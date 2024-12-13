#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>

/**
 * Compile DTS text to DTB binary.
 *
 * @param dts_buf: Pointer to the DTS source text.
 * @param size: Size of the DTS source buffer.
 * @param dtb_size: Pointer to an integer where the DTB size will be stored.
 * @return: Pointer to the DTB buffer, or NULL on error. Caller must free the buffer.
 */
char* compile_dts_to_dtb(const char* dts_buf, int size, int* dtb_size) {
    if (!dts_buf || size <= 0 || !dtb_size) {
        fprintf(stderr, "Invalid input parameters.\n");
        return NULL;
    }

    char dts_template[] = "/tmp/temp_dts_XXXXXX.dts";
    char dtb_template[] = "/tmp/temp_dtb_XXXXXX.dtb";
    int dts_fd = -1, dtb_fd = -1;
    char* dts_filename = NULL;
    char* dtb_filename = NULL;
    FILE* dts_file = NULL;
    FILE* dtb_file = NULL;
    char* dtb_buffer = NULL;
    pid_t pid;
    int status;

    // Create temporary DTS file
    dts_fd = mkstemps(dts_template, 4); // 4 bytes for ".dts"
    if (dts_fd == -1) {
        perror("mkstemps for DTS failed");
        goto cleanup;
    }
    dts_filename = strdup(dts_template);
    if (!dts_filename) {
        perror("strdup for DTS filename failed");
        goto cleanup;
    }

    // Write DTS buffer to temporary DTS file
    dts_file = fdopen(dts_fd, "w");
    if (!dts_file) {
        perror("fdopen for DTS file failed");
        goto cleanup;
    }
    if (fwrite(dts_buf, 1, size, dts_file) != (size_t)size) {
        perror("fwrite to DTS file failed");
        goto cleanup;
    }
    fclose(dts_file);
    dts_file = NULL;

    // Create temporary DTB file
    dtb_fd = mkstemps(dtb_template, 4); // 4 bytes for ".dtb"
    if (dtb_fd == -1) {
        perror("mkstemps for DTB failed");
        goto cleanup;
    }
    dtb_filename = strdup(dtb_template);
    if (!dtb_filename) {
        perror("strdup for DTB filename failed");
        goto cleanup;
    }
    close(dtb_fd); // dtc will create the DTB file

    // Fork a child process to execute dtc
    pid = fork();
    if (pid == -1) {
        perror("fork failed");
        goto cleanup;
    } else if (pid == 0) {
        // Child process
        execlp("dtc", "dtc", "-I", "dts", "-O", "dtb", "-o", dtb_filename, dts_filename, (char*)NULL);
        // If execlp fails
        perror("execlp failed");
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        while (1) {
            if (waitpid(pid, &status, 0) == -1) {
                if (errno == EINTR) {
                    continue;  // If interrupted by a signal, try again
                } else {
                    perror("waitpid failed");
                    exit(EXIT_FAILURE);  // Exit if waitpid failed for reasons other than EINTR
                }
            } else {
                break;  // Successfully waited for child process
            }
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fprintf(stderr, "dtc failed to compile DTS to DTB.\n");
            goto cleanup;
        }
    }

    // Open the DTB file for reading
    dtb_file = fopen(dtb_filename, "rb");
    if (!dtb_file) {
        perror("fopen for DTB file failed");
        goto cleanup;
    }

    // Determine the size of the DTB file
    if (fseek(dtb_file, 0, SEEK_END) != 0) {
        perror("fseek failed");
        goto cleanup;
    }
    long temp_size = ftell(dtb_file);
    if (temp_size == -1) {
        perror("ftell failed");
        goto cleanup;
    }
    rewind(dtb_file);

    // Allocate buffer for DTB
    dtb_buffer = (char*)malloc(temp_size);
    if (!dtb_buffer) {
        perror("malloc for DTB buffer failed");
        goto cleanup;
    }

    // Read DTB file into buffer
    if (fread(dtb_buffer, 1, temp_size, dtb_file) != (size_t)temp_size) {
        perror("fread from DTB file failed");
        free(dtb_buffer);
        dtb_buffer = NULL;
        goto cleanup;
    }
    fclose(dtb_file);
    dtb_file = NULL;

    *dtb_size = (int)temp_size;

    // Cleanup temporary files
cleanup:
    if (dts_file) fclose(dts_file);
    if (dtb_file) fclose(dtb_file);
    if (dts_filename) {
        unlink(dts_filename);
        free(dts_filename);
    }
    if (dtb_filename) {
        unlink(dtb_filename);
        free(dtb_filename);
    }
    return dtb_buffer;
}

/**
 * Example usage of compile_dts_to_dtb function.
 */
char* create_spike_dtb(uint64_t memory_size, const char* append, int serial_int, int* dtb_size) {

    const char* dts_header =    "/dts-v1/;\n"
                                "\n"
                                "/ {\n"
                                "  #address-cells = <2>;\n"
                                "  #size-cells = <2>;\n"
                                "  compatible = \"ucbbar,spike-bare-dev\";\n"
                                "  model = \"ucbbar,spike-bare\";\n";
    const char* dts_chosen_format =
                                "  chosen {\n"
                                "    stdout-path = &SERIAL0;\n"
                                "    bootargs = \"%s\";\n"
                                "  };\n";
const char* dts_cpu =
                                "  cpus {\n"
                                "    #address-cells = <1>;\n"
                                "    #size-cells = <0>;\n"
                                "    timebase-frequency = <10000000>;\n"
                                "    CPU0: cpu@0 {\n"
                                "      device_type = \"cpu\";\n"
                                "      reg = <0>;\n"
                                "      status = \"okay\";\n"
                                "      compatible = \"riscv\";\n"
                                "      riscv,isa = \"rv64imafdc_zicntr_zihpm\";\n"
                                "      mmu-type = \"riscv,sv57\";\n"
                                "      riscv,pmpregions = <16>;\n"
                                "      riscv,pmpgranularity = <4>;\n"
                                "      clock-frequency = <1000000000>;\n"
                                "      CPU0_intc: interrupt-controller {\n"
                                "        #address-cells = <2>;\n"
                                "        #interrupt-cells = <1>;\n"
                                "        interrupt-controller;\n"
                                "        compatible = \"riscv,cpu-intc\";\n"
                                "      };\n"
                                "    };\n"
                                "  };\n";
const char* dts_memory_format =
                                "  memory@80000000 {\n"
                                "    device_type = \"memory\";\n"
                                "    reg = <0x0 0x80000000 0x%x 0x%x>;\n"
                                "  };\n";
const char* dts_soc_head =
                                "  soc {\n"
                                "    #address-cells = <2>;\n"
                                "    #size-cells = <2>;\n"
                                "    compatible = \"ucbbar,spike-bare-soc\", \"simple-bus\";\n"
                                "    ranges;\n"
                                "    clint@2000000 {\n"
                                "      compatible = \"riscv,clint0\";\n"
                                "      interrupts-extended = <&CPU0_intc 3 &CPU0_intc 7 >;\n"
                                "      reg = <0x0 0x2000000 0x0 0xc0000>;\n"
                                "    };\n"
                                "    PLIC: plic@c000000 {\n"
                                "      compatible = \"riscv,plic0\";\n"
                                "      #address-cells = <2>;\n"
                                "      interrupts-extended = <&CPU0_intc 11 &CPU0_intc 9 >;\n"
                                "      reg = <0x0 0xc000000 0x0 0x1000000>;\n"
                                "      riscv,ndev = <0x1f>;\n"
                                "      riscv,max-priority = <0xf>;\n"
                                "      #interrupt-cells = <1>;\n"
                                "      interrupt-controller;\n"
                                "    };\n";
const char* dts_serial_head =
                                "    SERIAL0: ns16550@10000000 {\n"
                                "      compatible = \"ns16550a\";\n"
                                "      clock-frequency = <10000000>;\n";
const char* dts_serial_int =
                                "      interrupt-parent = <&PLIC>;\n"
                                "      interrupts = <1>;\n";
const char* dts_serial_tail =
                                "      reg = <0x0 0x10000000 0x0 0x100>;\n"
                                "      reg-shift = <0x0>;\n"
                                "      reg-io-width = <0x1>;\n"
                                "    };\n";
const char* dts_soc_tail =
                                "    pmc: power-management@100d0000 {\n"
                                "            compatible = \"syscon\", \"simple-mfd\";\n"
                                "            reg = <0x0 0x100d0000 0x0 0x58>;\n"
                                "\n"
                                "            syscon-reboot {\n"
                                "                    compatible = \"syscon-reboot\";\n"
                                "                    offset = <0x30>;\n"
                                "                    mask = <0x1>;\n"
                                "            };\n"
                                "\n"
                                "            syscon-poweroff {\n"
                                "                    compatible = \"syscon-poweroff\";\n"
                                "                    regmap = <&pmc>;\n"
                                "                    offset = <0x14>;\n"
                                "                    mask = <0x3c00>;\n"
                                "                    value = <0x3c00>;\n"
                                "            };\n"
                                "    };\n"
                                "  };\n"
                                "//  htif {\n"
                                "//    compatible = \"ucb,htif0\";\n"
                                "//  };\n";
const char* dts_tail =
                                "};";


    char* dts_chosen = malloc(0x1000);
    assert(dts_chosen);
    char* dts_memory = malloc(0x1000);
    assert(dts_memory);

    snprintf(dts_chosen, 0x1000, dts_chosen_format, append);
    snprintf(dts_memory, 0x1000, dts_memory_format, memory_size >> 32, memory_size & 0xffffffff);

    char* dts = calloc(1, 0x100000);
    assert(dts);
    strcat(dts, dts_header);
    if (dts_chosen) strcat(dts, dts_chosen);
    if (dts_cpu) strcat(dts, dts_cpu);
    if (dts_memory) strcat(dts, dts_memory);
    if (dts_soc_head) strcat(dts, dts_soc_head);
    if (dts_serial_head) strcat(dts, dts_serial_head);
    if (serial_int) strcat(dts, dts_serial_int);
    if (dts_serial_tail) strcat(dts, dts_serial_tail);
    if (dts_soc_tail) strcat(dts, dts_soc_tail);

    strcat(dts, dts_tail);


    int dts_size = strlen(dts);
    return compile_dts_to_dtb(dts, dts_size, dtb_size);

    // if (dtb_buf) {
    //     printf("DTB compiled successfully. Size: %d bytes\n", dtb_size);
    //     // You can now use the dtb_buf as needed
    //     // For demonstration, we'll write it to a file
    //     FILE* out = fopen("output.dtb", "wb");
    //     if (out) {
    //         fwrite(dtb_buf, 1, dtb_size, out);
    //         fclose(out);
    //         printf("DTB written to output.dtb\n");
    //     } else {
    //         perror("Failed to write DTB to file");
    //     }
    //     // free(dtb_buf);
    //     return dtb_buf;
    // } else {
    //     fprintf(stderr, "Failed to compile DTS to DTB.\n");
    //     return NULL;
    // }
}