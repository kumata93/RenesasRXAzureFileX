# Renesas RX65N FileX + LevelX ポーティング解説および利用ガイド

本ドキュメントでは、ルネサス RX65N マイコンの内蔵データフラッシュ（32KB）上で、**Azure RTOS / Eclipse FileX (FAT12)** および **LevelX (NOR Flash ウェアレベリング FTL)** を動作させるためのポーティング設計、ハードウェア固有の課題と解決策、ならびにサンプルの実行手順について解説します。

---

## 1. 全体アーキテクチャ

システムは以下の階層構造で構成されています。

```text
+--------------------------------------------------------------+
|            アプリケーション層 (sample_filex_levelx.c)          |
|    fx_file_create / fx_file_write / fx_file_read             |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|             ファイルシステム層 (Eclipse FileX v6.2.1)          |
|    FAT12 / ディレクトリ管理 / クラスタ管理 / セクタI/O要求     |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|            アダプタ層 (src/driver/fx_levelx_nor_driver.c)     |
|    FileX Driver Request (READ, WRITE, BOOT, INIT) の受付     |
|    LevelX セクタAPI (lx_nor_flash_sector_read/write) へ変換   |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|            FTL 摩耗平滑化層 (Eclipse LevelX NOR v6.2.1)       |
|    物理セクタ ⇄ 論理セクタ動的マッピング                       |
|    消去回数平滑化 / ガベージコレクション                      |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|        LevelX ポート層 (src/driver/lx_nor_rx65n_dataflash.c)  |
|    4KB 仮想 NOR ブロック ⇄ 64B 物理ブロックの集約と抽象化       |
|    read / write / block_erase / block_erased_verify コールバック |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|    低水準フラッシュ制御層 (src/driver/rx65n_dataflash.c)       |
|    FACI 直接コマンド制御 (0x007E0000)                         |
|    RX65N 不定値対策: 4B単位プログラムビットマップによる仮想0xFF |
|    NOR ビットクリア上書き模倣: 64B ブロック消去・書き戻し       |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|          RX65N 内蔵データフラッシュ (32KB: 0x00100000 - 0x00107FFF) |
|    消去単位: 64 bytes (512ブロック) / プログラム単位: 4 bytes  |
+--------------------------------------------------------------+
```

---

## 2. RX65N データフラッシュ固有の課題とポーティング解決策

RX65N（RV40F プロセス）の内蔵データフラッシュは、一般的な外部 SPI/パラレル NOR フラッシュと仕様が大きく異なり、LevelX NOR ドライバをそのまま適用すると正常に動作しません。本プロジェクトでは以下の 3 つの課題をソフトウェア層で解決しています。

### 課題 1: ブロック消去直後の読出し値が不定（Undefined）
- **現象**: 
  RX65N のハードウェア仕様上、消去直後のデータフラッシュ領域を CPU のメモリマップドリード（`0x00100000`〜）で読み出すと、`0xFFFFFFFF` ではなく不定なランダムビット列（`0xAF092831` 等）が返されます。一方、ハードウェアの FACI `BlankCheck` では正しく `BLANK`（消去済み）と判定されます。
- **LevelX での問題**: 
  LevelX は NOR フラッシュが消去直後に `0xFFFFFFFF` を返す前提で動作します。不定値が返ると「すでにフォーマット済みの別データが存在する」と誤認し、`min_erased_count` の走査に失敗して `0x01` (LX_ERROR) や `0x5A` (LX_SYSTEM_INVALID_FORMAT) でオープンに失敗します。
- **解決策**:
  - `rx65n_dataflash.c` 内に 4 バイトワードごとのプログラム状態を記録するビットマップ（`s_df_written_map`: 8,192 ワード = 1KB RAM）を配置。
  - `rx65n_dataflash_read()` では、未プログラム（消去済み）のワードに対して **仮想的に `0xFF` を返却**。
  - マイコン起動・リセット時は、64 バイト単位でハードウェア `BlankCheck` を行い、非ブランクのブロックのみ 4 バイト単位で `BlankCheck` を実行してビットマップを高速かつ正確に再構築。

### 課題 2: 同一ワードへの再プログラム不可（消去なしの追加書き込み禁止）
- **現象**:
  LevelX は NOR フラッシュの「ビットを 1 から 0 へ落とす上書き（In-place bit clearing）」を利用して、セクタ割り当てテーブルや消去カウンタの更新を行います。しかし、RX65N のデータフラッシュは 1 度プログラムした 4 バイトワードに再度プログラムコマンドを発行すると `FSTATR.PRGERR = 1`, `FASTAT.CMDLK = 1`（プログラムエラー・コマンドロック）が発生します。
- **解決策**:
  - `rx65n_dataflash_write()` において、書き込み先ワードが未プログラムの場合は通常通り高速にプログラム（連続ワードはバッチ実行）。
  - 書き込み先ワードが既にプログラム済みで、かつ **値が変更される場合** は、その 64 バイト物理ブロックの内容を RAM 上に退避・マージし、当該 64 バイトブロックを FACI で消去した上で再書き込みを行う **「64B ブロック書き戻し（Read-Modify-Write）」** を自動実行。

### 課題 3: 消去単位のジオメトリ不整合（64 バイト vs LevelX 最小セクタ）
- **現象**:
  FileX の 1 セクタは 512 バイトであり、LevelX NOR は最低でも 1 セクタ以上のブロックサイズを要求します。RX65N のハードウェア消去単位は 64 バイトです。
- **解決策**:
  - 64 バイトの物理ブロックを 64 個束ね、**4,096 バイト（4KB）の「論理 NOR ブロック」** として LevelX に提示（合計 8 ブロック = 32KB）。
  - 1 論理ブロックあたり 512 バイトのセクタを複数格納し、LevelX の摩耗平滑化・ガベージコレクションを効率的に動作させます。

---

## 3. ソースコード構成と各モジュールの役割

| ディレクトリ / ファイル | 役割・内容 |
|:---|:---|
| `src/driver/rx65n_dataflash.c` / `.h` | **低水準データフラッシュドライバ**<br>- FACI レジスタ直接制御（モード移行、消去、プログラム）<br>- 4B 単位の書込み済みビットマップ管理<br>- 仮想 0xFF 読出しおよび NOR 上書きエミュレーション |
| `src/driver/lx_nor_rx65n_dataflash.c` / `.h` | **LevelX NOR フラッシュポートドライバ**<br>- LevelX インターフェース関数群の実装（`read`, `write`, `block_erase`, `block_erased_verify`, `system_error`）<br>- 4KB 論理ブロック ⇄ 64B ハードウェアブロック変換 |
| `src/driver/fx_levelx_nor_driver.c` / `.h` | **FileX - LevelX アダプタドライバ**<br>- FileX からのドライバリクエスト（`INIT`, `READ`, `WRITE`, `BOOT_READ`, `BOOT_WRITE`）を LevelX API に中継 |
| `src/app/sample_filex_levelx.c` / `.h` | **デモアプリケーション**<br>- ThreadX スレッドとして動作<br>- 未フォーマット検知時の全消去・初期 FAT12 フォーマット<br>- ファイル作成・書き込み・フラッシュ・読み戻しデータ検証 |
| `tests/` | **Unity ホストユニットテスト**<br>- ホスト PC (GCC / MinGW / Linux CI) 上で実行可能なシミュレータテスト環境 |

---

## 4. サンプルアプリケーションの動作フロー

`sample_filex_levelx.c` のデモシーケンスは下図の流れで実行されます。

```text
[Step 1: メディアオープン]
       |
       +--> fx_media_open(&levelx_media, ...)
       |
       |--[オープン成功 (フォーマット済み)]------------------------+
       |                                                          |
       v (オープン失敗 / 未フォーマット / ブート無効)                  |
[Step 2: 物理チップ全消去]                                          |
       | rx65n_dataflash_erase_all() (全512ブロック消去)           |
       v                                                          |
[Step 3: LevelX 明示オープン]                                       |
       | lx_nor_flash_open() (メタデータ初期化: erase_count=1)     |
       v                                                          |
[Step 4: FAT12 フォーマット]                                        |
       | fx_media_format() (32セクタ / 512B)                       |
       v                                                          |
[Step 5: メディア再オープン]                                        |
       | fx_media_open()                                          |
       |                                                          |
       +<---------------------------------------------------------+
       |
       v
[Step 6: ファイル作成 & オープン]
       | fx_file_create("CONFIG.TXT")
       | fx_file_open("CONFIG.TXT", FX_OPEN_FOR_WRITE)
       v
[Step 7: データ書き込み & フラッシュ]
       | fx_file_write("Renesas RX65N FileX + LevelX NOR Flash Test OK!...")
       | fx_file_close() / fx_media_flush()
       v
[Step 8: 読み出し & ベリファイ]
       | fx_file_open("CONFIG.TXT", FX_OPEN_FOR_READ)
       | fx_file_read()
       | memcmp() によるデータ完全一致検証
       v
[Step 9: 完了 & メディアクローズ]
       | fx_media_close()
```

---

## 5. ビルドと実行方法

### 開発環境
- **IDE**: Renesas e2 studio
- **コンパイラ**: GCC for Renesas RX 8.3.0 / 14.2.0
- **ターゲットボード**: RX65N 搭載ボード (Target Board for RX65N / Envision Kit 等)
- **ログ出力**: e2 studio の **Renesas Debug Console**（GDB / セミホスティング標準出力）を使用

### コマンドラインでのビルド手順
```powershell
# HardwareDebug フォルダへ移動
cd HardwareDebug

# クリーン
mingw32-make clean

# 並列ビルド
mingw32-make -j4 all
```
ビルド完了後、`filex_renesas_rx.elf` および `filex_renesas_rx.mot` が生成されます。

### 実行と出力ログの見方

#### 1回目：初回起動時（未初期化フラッシュからの自動復旧とフォーマット）
```text
===============================================================
  Renesas RX65N FileX + LevelX NOR Flash (32KB Data Flash) Demo
===============================================================
[Demo] Step 1: Opening media via fx_levelx_nor_driver...
  [Driver] FX_DRIVER_INIT called...
  [Driver] Opening LevelX NOR flash instance...
[LevelX_Port] initialize called (nor_flash=0x43e0)...
[LevelX_Port] initialize completed: base=0x00100000, blocks=8, words/blk=1024
  [Driver] lx_nor_flash_open returned: 0x00 (state=0x4E4F524F)
  [Driver] FX_DRIVER_BOOT_READ: invalid boot signature (sig[0]=0xFF, [510]=0xFF, [511]=0xFF) -> FX_MEDIA_INVALID
  [Driver] FX_DRIVER_UNINIT called...
  [Driver] lx_nor_flash_close returned: 0x00
[Demo] Media open returned status 0x01.
[Demo] Step 2: Performing physical chip erase of Data Flash (32KB, 512 blocks)...
  [DF_HW] Erasing all 512 blocks (0x00100000 - 0x00107FFF)...
  [DF_HW] Block 0 (0x00100000) .. Block 511 (0x00107FC0) erased OK
  [DF_HW] All 512 blocks erased successfully. Returned to read mode.
  [DF_HW] Virtual read check at 0x00100000: 0xFFFFFFFF (Expected: 0xFFFFFFFF)
[Demo] Data Flash physical chip erase completed successfully.
[Demo] Step 3: Explicitly opening LevelX NOR Flash instance to initialize NOR metadata...
[LevelX_Port] initialize called (nor_flash=0x43e0)...
[LevelX_Port] initialize completed: base=0x00100000, blocks=8, words/blk=1024
[Demo] Explicit lx_nor_flash_open returned: 0x00 (state=0x4E4F524F)
[Demo] Step 4: Formatting Data Flash via LevelX NOR Driver as FAT12 (32 sectors)...
  [Driver] FX_DRIVER_INIT called...
  [Driver] LevelX NOR flash is already opened (state=LX_NOR_FLASH_OPENED).
  [Driver] FX_DRIVER_BOOT_WRITE called for sector 0...
  [Driver] lx_nor_flash_sector_write for boot sector 0 returned: 0x00
  [Driver] FX_DRIVER_WRITE: start_sec=1, count=1
  [Driver] FX_DRIVER_WRITE: start_sec=2, count=1
  [Driver] FX_DRIVER_WRITE: start_sec=3, count=1
  [Driver] FX_DRIVER_UNINIT called...
  [Driver] lx_nor_flash_close returned: 0x00
[Demo] FAT12 format completed successfully.
[Demo] Step 5: Re-opening media after format...
  [Driver] FX_DRIVER_INIT called...
  [Driver] Opening LevelX NOR flash instance...
[LevelX_Port] initialize called (nor_flash=0x43e0)...
[LevelX_Port] initialize completed: base=0x00100000, blocks=8, words/blk=1024
  [Driver] lx_nor_flash_open returned: 0x00 (state=0x4E4F524F)
  [Driver] FX_DRIVER_BOOT_READ: Valid boot sector found.
[Demo] Media opened successfully.
[Demo] Creating file: CONFIG.TXT...
[Demo] File CONFIG.TXT created successfully.
[Demo] Opening CONFIG.TXT for writing...
[Demo] Writing 64 bytes: Renesas RX65N FileX + LevelX NOR Flash Test OK! [Cycle #1]
  [Driver] FX_DRIVER_WRITE: start_sec=2, count=1
  [Driver] FX_DRIVER_WRITE: start_sec=4, count=1
  [Driver] FX_DRIVER_WRITE: start_sec=2, count=1
  [Driver] FX_DRIVER_WRITE: start_sec=1, count=1
[Demo] File written and closed successfully.
[Demo] Opening CONFIG.TXT for reading...
[Demo] Read payload (64 bytes): Renesas RX65N FileX + LevelX NOR Flash Test OK! [Cycle #1]
[Demo] >>> DATA VERIFICATION SUCCESS: Data matches perfectly! <<<
  [Driver] FX_DRIVER_UNINIT called...
  [Driver] lx_nor_flash_close returned: 0x00
[Demo] Media closed successfully. Demo completed.
===============================================================
```

#### 2回目：再起動時（フォーマットをスキップし、不揮発データをそのまま利用）
```text
===============================================================
  Renesas RX65N FileX + LevelX NOR Flash (32KB Data Flash) Demo
===============================================================
[Demo] Step 1: Opening media via fx_levelx_nor_driver...
  [Driver] FX_DRIVER_INIT called...
  [Driver] Opening LevelX NOR flash instance...
[LevelX_Port] initialize called (nor_flash=0x43e0)...
[LevelX_Port] initialize completed: base=0x00100000, blocks=8, words/blk=1024
  [Driver] lx_nor_flash_open returned: 0x00 (state=0x4E4F524F)
  [Driver] FX_DRIVER_BOOT_READ: Valid boot sector found.
[Demo] Media opened successfully.
[Demo] Creating file: CONFIG.TXT...
[Demo] File CONFIG.TXT already exists on media.
[Demo] Opening CONFIG.TXT for writing...
[Demo] Writing 64 bytes: Renesas RX65N FileX + LevelX NOR Flash Test OK! [Cycle #1]
  [Driver] FX_DRIVER_WRITE: start_sec=4, count=1
  [Driver] FX_DRIVER_WRITE: start_sec=2, count=1
[Demo] File written and closed successfully.
[Demo] Opening CONFIG.TXT for reading...
[Demo] Read payload (64 bytes): Renesas RX65N FileX + LevelX NOR Flash Test OK! [Cycle #1]
[Demo] >>> DATA VERIFICATION SUCCESS: Data matches perfectly! <<<
  [Driver] FX_DRIVER_UNINIT called...
  [Driver] lx_nor_flash_close returned: 0x00
[Demo] Media closed successfully. Demo completed.
===============================================================
```

---

## 6. 自作プロジェクトへの組み込み手順

本ポーティングをご自身のプロジェクトに移植・活用する場合は、以下のステップで行います。

1. **ソースコードの配置**
   `src/driver/` 配下の以下のファイルをプロジェクトへ追加します：
   - `rx65n_dataflash.c` / `rx65n_dataflash.h`
   - `lx_nor_rx65n_dataflash.c` / `lx_nor_rx65n_dataflash.h`
   - `fx_levelx_nor_driver.c` / `fx_levelx_nor_driver.h`

2. **FIT モジュール / ペリフェラルの有効化**
   Smart Configurator 等で `r_flash_rx` (Code/Data Flash ドライバ) をプロジェクトに組み込み、データフラッシュアクセスを有効化（`FLASH_CFG_DATA_FLASH_BGO = 0` (ブロッキングモード推奨)）します。

3. **初期化とメディアオープン**
   ```c
   #include "fx_api.h"
   #include "lx_api.h"
   #include "fx_levelx_nor_driver.h"

   static FX_MEDIA g_media;
   static UCHAR    g_media_memory[512];

   void app_filesystem_init(void)
   {
       UINT status;

       /* 1. LevelX および FileX サブシステムの初期化 */
       lx_nor_flash_initialize();
       fx_system_initialize();

       /* 2. メディアオープン */
       status = fx_media_open(&g_media, "DF_MEDIA", fx_levelx_nor_driver, FX_NULL,
                              g_media_memory, sizeof(g_media_memory));

       if (status != FX_SUCCESS)
       {
           /* 未フォーマットの場合、フォーマットを実行 */
           fx_media_format(&g_media, fx_levelx_nor_driver, FX_NULL,
                           g_media_memory, sizeof(g_media_memory),
                           "DF_VOLUME", 1, 32, 0, 32, 512, 1, 1, 1);

           /* フォーマット後に再オープン */
           fx_media_open(&g_media, "DF_MEDIA", fx_levelx_nor_driver, FX_NULL,
                         g_media_memory, sizeof(g_media_memory));
       }

       /* 3. これ以降、fx_file_create / write / read 等が通常通り利用可能 */
   }
   ```
