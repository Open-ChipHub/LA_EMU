#include "qemu/osdep.h"
#include "util.h"
#include "device_io.h"

// Array to hold registered IO devices
static io_device_t io_devices[IO_DEV_MAX];

// Registers a new IO device
int io_register_device(void *opaque,
                       ioport_read_func read,
                       ioport_write_func write,
                       io_fini_func fini,
                       uint64_t base_addr,
                       unsigned size) {

    // Check for available slot
    int slot = -1;
    for (int i = 0; i < IO_DEV_MAX; i++) {
        if (!io_devices[i].in_use) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        fprintf(stderr, "Error: Maximum IO devices reached (%d)\n", IO_DEV_MAX);
        return -1;
    }

    // Check for address range overlap
    for (int i = 0; i < IO_DEV_MAX; i++) {
        if (io_devices[i].in_use) {
            uint64_t existing_start = io_devices[i].base_addr;
            uint64_t existing_end = existing_start + io_devices[i].size;
            uint64_t new_start = base_addr;
            uint64_t new_end = new_start + size;

            if ((new_end > existing_start) && (new_start < existing_end)) {
                fprintf(stderr, "Error: Address range overlaps with an existing device\n");
                return -1;
            }
        }
    }

    // Register the device
    io_devices[slot].opaque = opaque;
    io_devices[slot].read = read;
    io_devices[slot].write = write;
    io_devices[slot].fini = fini;
    io_devices[slot].base_addr = base_addr;
    io_devices[slot].size = size;
    io_devices[slot].in_use = 1;

    printf("IO Device registered at base address 0x%lX, size %u\n", base_addr, size);

    return 0;
}

/*
// Unregisters an existing IO device based on base address
int io_unregister_device(uint64_t base_addr) {

    for (int i = 0; i < IO_DEV_MAX; i++) {
        if (io_devices[i].in_use && io_devices[i].base_addr == base_addr) {
            // Call the fini function
            if (io_devices[i].fini) {
                io_devices[i].fini(io_devices[i].opaque);
            }
            // Mark the slot as unused
            io_devices[i].in_use = 0;
            printf("IO Device at base address 0x%lX unregistered\n", base_addr);
            return 0;
        }
    }

    fprintf(stderr, "Error: IO Device with base address 0x%lX not found\n", base_addr);
    return -1;
}
*/

// Reads from a specified address by delegating to the appropriate device
uint64_t io_read(uint64_t addr, unsigned size) {
    for (int i = 0; i < IO_DEV_MAX; i++) {
        if (io_devices[i].in_use) {
            uint64_t start = io_devices[i].base_addr;
            uint64_t end = start + io_devices[i].size;
            if (addr >= start && addr + size <= end) {
                // Delegate the read to the device's read function
                uint64_t value = io_devices[i].read(io_devices[i].opaque, addr - start, size);
                return value;
            }
        }
    }

    fprintf(stderr, "Warning: No IO device handles address 0x%lX, read\n", addr);
    return 0;
}

// Writes to a specified address by delegating to the appropriate device
void io_write(uint64_t addr, uint64_t val, unsigned size) {
    for (int i = 0; i < IO_DEV_MAX; i++) {
        if (io_devices[i].in_use) {
            uint64_t start = io_devices[i].base_addr;
            uint64_t end = start + io_devices[i].size;
            if (addr >= start && addr + size <= end) {
                // Delegate the write to the device's write function
                io_devices[i].write(io_devices[i].opaque, addr - start, val, size);
                return;
            }
        }
    }

    fprintf(stderr, "Warning: No IO device handles address 0x%lX, write, %lx\n", addr, val);
}

// Cleans up all registered IO devices by calling their fini functions
void __attribute__((destructor)) io_fini_all() {
    for (int i = 0; i < IO_DEV_MAX; i++) {
        if (io_devices[i].in_use) {
            if (io_devices[i].fini) {
                io_devices[i].fini(io_devices[i].opaque);
                printf("IO Device at base address 0x%lX cleaned up\n", io_devices[i].base_addr);
            }
            io_devices[i].in_use = 0;
        }
    }
}