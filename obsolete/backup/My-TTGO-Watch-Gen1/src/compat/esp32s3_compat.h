#pragma once

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_ESP32S3_DEV)

#ifndef VSPI
#define VSPI FSPI
#endif

#ifndef VSPI_HOST
#define VSPI_HOST SPI2_HOST
#endif

#ifndef VSPIQ_IN_IDX
#define VSPIQ_IN_IDX FSPIQ_IN_IDX
#endif

#ifndef VSPID_OUT_IDX
#define VSPID_OUT_IDX FSPID_OUT_IDX
#endif

#ifndef GPIO_NUM_22
#define GPIO_NUM_22 22
#endif

#ifndef GPIO_NUM_23
#define GPIO_NUM_23 23
#endif

#ifndef GPIO_NUM_24
#define GPIO_NUM_24 24
#endif

#ifndef GPIO_NUM_25
#define GPIO_NUM_25 25
#endif

#ifndef SPI_MOSI_DLEN_REG
#define SPI_MOSI_DLEN_REG(i) SPI_MS_DLEN_REG(i)
#endif

#ifndef SPI_USR_MOSI_DBITLEN
#define SPI_USR_MOSI_DBITLEN SPI_MS_DATA_BITLEN
#endif

#ifndef SPI_USR_MOSI_DBITLEN_S
#define SPI_USR_MOSI_DBITLEN_S SPI_MS_DATA_BITLEN_S
#endif


#endif
