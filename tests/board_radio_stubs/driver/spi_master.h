#pragma once
#include <stddef.h>
typedef void* spi_device_handle_t;
typedef int spi_host_device_t;
enum {SPI_DMA_DISABLED=0};
typedef struct {int mosi_io_num,miso_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num,max_transfer_sz;} spi_bus_config_t;
typedef struct {int clock_speed_hz,mode,spics_io_num,queue_size;} spi_device_interface_config_t;
typedef struct {size_t length;const void* tx_buffer;void* rx_buffer;} spi_transaction_t;
int spi_bus_initialize(spi_host_device_t,const spi_bus_config_t*,int);
int spi_bus_add_device(spi_host_device_t,const spi_device_interface_config_t*,spi_device_handle_t*);
int spi_bus_remove_device(spi_device_handle_t);
int spi_bus_free(spi_host_device_t);
int spi_device_polling_transmit(spi_device_handle_t,spi_transaction_t*);

int spi_bus_get_max_transaction_len(spi_host_device_t,size_t*);
