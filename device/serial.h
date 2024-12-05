#ifndef __SERIAL_H__
#define __SERIAL_H__

uint64_t serial_ioport_read(void *opaque, uint64_t addr, unsigned size);
void serial_ioport_write(void *opaque, uint64_t addr, uint64_t val, unsigned size);

#endif
