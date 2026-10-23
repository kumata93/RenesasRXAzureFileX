# Renesas RX65N FileX + LevelX Porting Guide & Usage Manual

This document details the software architecture, hardware-specific porting challenges, solutions, and sample application usage for running **Eclipse FileX (FAT12 File System)** and **Eclipse LevelX (NOR Flash Wear-Leveling FTL)** on the **Renesas RX65N on-chip Data Flash (32KB)**.

---

## 1. Overall Architecture

The storage stack is divided into well-defined layers as illustrated below:

```text
+--------------------------------------------------------------+
|            Application Layer (sample_filex_levelx.c)          |
|    fx_file_create / fx_file_write / fx_file_read             |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|             File System Layer (Eclipse FileX v6.2.1)          |
|    FAT12 / Directory & Cluster Management / Sector I/O        |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|            Adapter Layer (src/driver/fx_levelx_nor_driver.c)  |
|    Dispatches FileX Driver Requests (READ, WRITE, BOOT, INIT)|
|    Translates to LevelX Sector APIs (lx_nor_flash_sector_*)   |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|            FTL Wear-Leveling Layer (Eclipse LevelX NOR v6.2.1)|
|    Logical-to-Physical Sector Dynamic Mapping                |
|    Erase Count Leveling & Garbage Collection                 |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|        LevelX Port Layer (src/driver/lx_nor_rx65n_dataflash.c)|
|    Aggregates 64B HW Blocks into 4KB Logical NOR Blocks       |
|    LevelX Driver Callbacks (read/write/block_erase/verify)   |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|    Low-Level Flash Control (src/driver/rx65n_dataflash.c)    |
|    Direct FACI Command Area Access (0x007E0000)               |
|    4B Word Programmed Bitmap (Virtual 0xFF Erased Semantics)  |
|    NOR In-Place Overwrite Emulation via 64B Block Rewrite    |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|          RX65N On-Chip Data Flash (32KB: 0x00100000-0x00107FFF)|
|    Erase Unit: 64 bytes (512 blocks) / Program Unit: 4 bytes  |
+--------------------------------------------------------------+
```

---

## 2. Hardware Characteristics & Porting Solutions

The RX65N on-chip Data Flash (RV40F silicon process) has specific hardware constraints that differ fundamentally from standard SPI/parallel NOR flash memories. The following three major challenges were resolved in software:

### Challenge 1: Erased Flash Memory Reads as Undefined Data
- **Problem**:
  On RX65N hardware, CPU memory-mapped reads (`0x00100000`–`0x00107FFF`) of erased Data Flash blocks do **not** return clean `0xFFFFFFFF`. Instead, they return floating/unstable random bit patterns (e.g., `0xAF092831`). However, hardware FACI blank check (`R_FLASH_BlankCheck`) correctly detects `FLASH_RES_BLANK`.
- **LevelX Impact**:
  LevelX NOR flash requires clean `0xFFFFFFFF` upon initial scan to identify unformatted media and compute `min_erased_count`. Random bits cause LevelX to misdetect invalid existing format metadata, failing initialization with `0x01` (`LX_ERROR`) or `0x5A` (`LX_SYSTEM_INVALID_FORMAT`).
- **Solution**:
  - Maintained a word-level programmed status bitmap (`s_df_written_map`: 8,192 words = 1 KB RAM) in `rx65n_dataflash.c`.
  - In `rx65n_dataflash_read()`, any word marked unprogrammed returns **virtual `0xFFFFFFFF`**, while programmed words read directly from flash memory.
  - On cold boot or reset, `rx65n_dataflash_map_init_from_hw()` executes a fast 64-byte `BlankCheck` across all 512 blocks. For non-blank blocks, it refines the check with 4-byte `BlankCheck` per word to perfectly restore the bitmap in RAM without data loss.

### Challenge 2: Inability to Reprogram Programmed Words Without Erase
- **Problem**:
  LevelX updates block allocation tables and erase counters by performing NOR-style in-place bit-clearing (writing `1 -> 0` on existing words). However, RX65N Data Flash hardware prohibits programming an already programmed 4-byte word without first erasing its 64-byte block; doing so sets `FSTATR.PRGERR = 1` and `FASTAT.CMDLK = 1` (Program Error & Command Lock).
- **Solution**:
  - `rx65n_dataflash_write()` checks the bitmap for each 4-byte word:
    1. **Unprogrammed word**: Directly programs via FACI (batches consecutive runs for performance).
    2. **Programmed word with same value**: Skips programming (no-op).
    3. **Programmed word with new value**: Emulates NOR overwrite by buffering the 64-byte block in RAM, erasing the block via FACI, and rewriting the merged contents.

### Challenge 3: Geometry Mismatch (64B Hardware Block vs. 512B LevelX Sector)
- **Problem**:
  FileX standard sector size is 512 bytes, and LevelX NOR Flash requires blocks large enough to hold at least one sector plus metadata headers. RX65N hardware block size is only 64 bytes.
- **Solution**:
  - Aggregated 64 contiguous 64-byte hardware blocks into one **4,096-byte (4KB) Logical NOR Block**.
  - 32 KB Data Flash is presented to LevelX as **8 logical blocks** of 1,024 words (4KB) each, allowing LevelX wear-leveling and garbage collection to operate properly.

---

## 3. Source Tree & File Responsibilities

| Directory / File | Description & Responsibilities |
|:---|:---|
| `src/driver/rx65n_dataflash.c` / `.h` | **Low-Level Data Flash Driver**<br>- Direct FACI sequencer control (P/E mode, erase, program)<br>- 4B-word bitmap management (1 KB RAM)<br>- Virtual `0xFF` read emulation and 64B block Read-Modify-Write |
| `src/driver/lx_nor_rx65n_dataflash.c` / `.h` | **LevelX NOR Port Driver**<br>- Implements `LX_NOR_FLASH` driver callbacks (`read`, `write`, `block_erase`, `block_erased_verify`, `system_error`)<br>- Maps 4KB logical blocks to 64B physical blocks |
| `src/driver/fx_levelx_nor_driver.c` / `.h` | **FileX - LevelX Bridge Adapter**<br>- Receives FileX I/O requests (`INIT`, `READ`, `WRITE`, `BOOT_READ`, `BOOT_WRITE`) and delegates to LevelX sector APIs |
| `src/app/sample_filex_levelx.c` / `.h` | **Demo Application**<br>- Runs as a ThreadX thread<br>- Detects unformatted media, triggers chip erase and FAT12 format<br>- Creates, writes, flushes, reads back, and verifies `CONFIG.TXT` |
| `tests/` | **Host Unity Unit Tests**<br>- Runs on host machines (GCC / MinGW / Linux CI) simulating Data Flash and verifying LevelX/FileX formatting logic |

---

## 4. Application Execution Flow

```text
[Step 1: Open Media via fx_levelx_nor_driver]
       |
       +--> fx_media_open(&levelx_media, ...)
       |
       |--[Open Success (Already Formatted)]--------------------+
       |                                                        |
       v (Open Failed / Unformatted / Invalid Boot Record)      |
[Step 2: Physical Full-Chip Erase]                              |
       | rx65n_dataflash_erase_all() (512 blocks erased)        |
       v                                                        |
[Step 3: Explicit LevelX Instance Open]                         |
       | lx_nor_flash_open() (Initializes metadata, erase_cnt=1)|
       v                                                        |
[Step 4: Format Media as FAT12]                                 |
       | fx_media_format() (32 sectors, 512B/sector)            |
       v                                                        |
[Step 5: Re-Open Media]                                         |
       | fx_media_open()                                        |
       |                                                        |
       +<-------------------------------------------------------+
       |
       v
[Step 6: Create & Open File]
       | fx_file_create("CONFIG.TXT")
       | fx_file_open("CONFIG.TXT", FX_OPEN_FOR_WRITE)
       v
[Step 7: Write Data & Flush]
       | fx_file_write("Renesas RX65N FileX + LevelX NOR Flash Test OK!...")
       | fx_file_close() / fx_media_flush()
       v
[Step 8: Read Back & Verify]
       | fx_file_open("CONFIG.TXT", FX_OPEN_FOR_READ)
       | fx_file_read()
       | memcmp() data verification
       v
[Step 9: Close Media & Complete]
       | fx_media_close()
```

---

## 5. Build & Execution Guide

### Requirements
- **IDE**: Renesas e2 studio
- **Toolchain**: GCC for Renesas RX 8.3.0 or 14.2.0
- **Target Board**: Renesas Target Board for RX65N or Envision Kit
- **UART Terminal**: 115,200 bps, 8 data bits, no parity, 1 stop bit

### Command-Line Build
```powershell
# Navigate to HardwareDebug directory
cd HardwareDebug

# Clean
mingw32-make clean

# Parallel build
mingw32-make -j4 all
```
The output files `filex_renesas_rx.elf` and `filex_renesas_rx.mot` will be generated.

### Console Output Walkthrough

#### Run 1: First Boot (Automatic Recovery & Initial Formatting)
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

#### Run 2: Subsequent Boot (Preserves Existing FAT12 File System Without Reformat)
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

## 6. Integration Guide for Custom Projects

To integrate this driver stack into your own project:

1. **Add Port Drivers**
   Copy the following files from `src/driver/` into your project:
   - `rx65n_dataflash.c` / `rx65n_dataflash.h`
   - `lx_nor_rx65n_dataflash.c` / `lx_nor_rx65n_dataflash.h`
   - `fx_levelx_nor_driver.c` / `fx_levelx_nor_driver.h`

2. **Configure Flash Peripheral**
   In Smart Configurator, enable the `r_flash_rx` FIT module with blocking mode (`FLASH_CFG_DATA_FLASH_BGO = 0`).

3. **Initialize and Open Media in Application**
   ```c
   #include "fx_api.h"
   #include "lx_api.h"
   #include "fx_levelx_nor_driver.h"

   static FX_MEDIA g_media;
   static UCHAR    g_media_memory[512];

   void app_filesystem_init(void)
   {
       UINT status;

       /* 1. Initialize LevelX and FileX subsystems */
       lx_nor_flash_initialize();
       fx_system_initialize();

       /* 2. Open media */
       status = fx_media_open(&g_media, "DF_MEDIA", fx_levelx_nor_driver, FX_NULL,
                              g_media_memory, sizeof(g_media_memory));

       if (status != FX_SUCCESS)
       {
           /* Media is unformatted: format as FAT12 */
           fx_media_format(&g_media, fx_levelx_nor_driver, FX_NULL,
                           g_media_memory, sizeof(g_media_memory),
                           "DF_VOLUME", 1, 32, 0, 32, 512, 1, 1, 1);

           /* Re-open after format */
           fx_media_open(&g_media, "DF_MEDIA", fx_levelx_nor_driver, FX_NULL,
                         g_media_memory, sizeof(g_media_memory));
       }

       /* 3. Media is ready: call fx_file_create / write / read as needed */
   }
   ```
