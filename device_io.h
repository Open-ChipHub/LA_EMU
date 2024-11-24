#ifndef __DEVICE_IO_H__
#define __DEVICE_IO_H__

#include <stdint.h>

// Maximum number of IO devices
#define IO_DEV_MAX 16

// Function pointer types for IO operations
typedef uint64_t (*ioport_read_func)(void *opaque, uint64_t addr, unsigned size);
typedef void (*ioport_write_func)(void *opaque, uint64_t addr, uint64_t val, unsigned size);
typedef void (*io_fini_func)(void *opaque);

// Structure representing an IO device
typedef struct {
    void *opaque;                        // User-defined data
    ioport_read_func read;               // Read callback function
    ioport_write_func write;             // Write callback function
    io_fini_func fini;                   // Cleanup callback function
    uint64_t base_addr;                      // Base address of the IO device
    unsigned size;                       // Size of the address space
    int in_use;                          // Flag indicating if the slot is in use
} io_device_t;

// Initializes the IO simulator
int io_init();

// Registers a new IO device
// Returns 0 on success, -1 on failure
int io_register_device(void *opaque,
                       ioport_read_func read,
                       ioport_write_func write,
                       io_fini_func fini,
                       uint64_t base_addr,
                       unsigned size);

/*
// Unregisters an existing IO device based on address
// Returns 0 on success, -1 if device not found
int io_unregister_device(uint64_t base_addr);
*/

// Reads from a specified address
// Returns the read value, or 0 if no device handles the address
uint64_t io_read(uint64_t addr, unsigned size);

// Writes to a specified address
void io_write(uint64_t addr, uint64_t val, unsigned size);

// Cleans up all registered IO devices
void io_fini_all();


#endif