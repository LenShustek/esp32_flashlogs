This is a module for the ESP32 Wifi microcontroller that efficiently implements 
non-volatile storage for event logs using the SPI FLASH memory. 

The logs are circular: when the log becomes full, adding the next new entry 
causes 4K bytes of the oldest entries to be removed. (FLASH can only be erased 
in 4K blocks, and you can only write to memory that has been erased.) 
Entries may be retrieved in either order: starting with the newest, or the oldest. 
The log data will survive rebooting and FLASHing a new program.

This was developed using the Arduino IDE, but it may, mutatis mutandis, be 
useful in other IDE environments. 

The ESP32 has no EEPROM. All non-volatile storage is implemented using the same 
SPI FLASH memory that contains the program, and there is a "partition table" 
described at 
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html 
which defines areas of the FLASH memory that are used for different purposes. 

In the Arduino IDE you can configure the partition table by having a CSV file 
called partitions.csv in your sketch directory. For testing this esp32_flashlogs 
library we slightly reduce the partition devoted to SPIFFS (their FLASH file 
system) to leave 64K bytes at the end of the 4MB FLASH memory for a single log 
partition. We then use functions in the ESP32 partition API, 
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/spi_flash.html#api-reference-partition-table, 
to read, erase, and write the log data. Here is our partitions.csv file:

   # Name, Type, SubType, Offset,  Size, Flags
   nvs, data, nvs, 0x9000, 0x5000,
   otadata, data, ota, 0xe000, 0x2000,
   app0, app, ota_0, 0x10000, 0x140000,
   app1, app, ota_1, 0x150000,0x140000,
   spiffs, data, spiffs, 0x290000, 0x160000,
   log1, 0x4D, 0, 0x3f0000, 0x10000,

You can have multiple logs by adding more "log" lines, as long as you
adjust the numbers so that the partitions pack exactly into the 4MB memory.
All log partitions must use custom partition type 0x4D, be at least
8K bytes, and be a multiple of 4K bytes. 

Since the number of FLASH memory write cycles is limited, we are careful 
to do only one write for each new event added to the log. That means 
there is no log header in FLASH that describes state information 
like the number of entries and where the newest and oldest entries are. 
When the log is opened, the current state is derived by reading the 
entire log, and that information is stored and maintained in RAM until 
the log is closed or the processor is rebooted. 

Writing to FLASH memory can only change 1-bits to 0-bits. Erasing it sets all 
bits to 1, but that can only be done in 4K blocks. Because of that restriction, 
the size of the data in each log entry must be 4 less than a power of two up to 
4096, so 4, 12, 28, 60, 124, 252, 508, 1020, 2044, or 4092 bytes. 

The API for esp32_flashlogs is documented in the esp32_flashlogs.h header file,
and all the code is in esp32_flashlogs.cpp. There is a test program at
esp32_flashlogs.ino. It is written in the C subset of C++, because I hate C++.

Len Shustek
24 Dec 2021
