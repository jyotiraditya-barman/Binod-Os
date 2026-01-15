// === FILE: kernel.c ===
// Enhanced Binod OmS UI dwith better visuals and features
#include <stdint.h>
#include <stdarg.h>
#include "io.h"
#include "kstring.h"
#include "fs.h"
#include "ata.h"
#include "interrupt.h"
#include "bmp.h"
#include "vga_mode13.h"
#include "framebuffer.h"
#include "tetris.c"

// ========== UI CONFIGURATION ==========
#define UI_COLOR_TITLE       COLOR_CYAN
#define UI_COLOR_PROMPT      COLOR_LIGHT_GREEN
#define UI_COLOR_TEXT        COLOR_LIGHT_GRAY
#define UI_COLOR_SUCCESS     COLOR_GREEN
#define UI_COLOR_ERROR       COLOR_LIGHT_RED
#define UI_COLOR_FILE        COLOR_YELLOW
#define UI_COLOR_DIR         COLOR_LIGHT_BLUE
#define UI_COLOR_HIGHLIGHT   COLOR_WHITE

// ========== UI HELPER FUNCTIONS ==========
void ui_print_banner(void) {
    vga_set_color(UI_COLOR_TITLE, COLOR_BLACK);
    printf_k("+--------------------------------------------------------------+\n");
    printf_k("|                                                              |\n");
    printf_k("|                     B I N O D   O S                          |\n");
    printf_k("|                    Version 1.0.0                             |\n");
    printf_k("|                                                              |\n");
    printf_k("+--------------------------------------------------------------+\n\n");
}

void ui_print_divider(char ch) {
    vga_set_color(UI_COLOR_TEXT, COLOR_BLACK);
    for(int i = 0; i < 80; i++) putc_k(ch);
    putc_k('\n');
}

void ui_print_header(const char *text) {
    vga_set_color(UI_COLOR_HIGHLIGHT, COLOR_BLACK);
    printf_k("\n+--[ %s ]", text);
    for(int i = kstrlen(text) + 6; i < 78; i++) putc_k('-');
    printf_k("+\n");
}

void ui_print_footer(void) {
    vga_set_color(UI_COLOR_TEXT, COLOR_BLACK);
    printf_k("+");
    for(int i = 0; i < 78; i++) putc_k('-');
    printf_k("+\n");
}

void ui_print_info(const char *fmt, ...) {
    vga_set_color(UI_COLOR_TEXT, COLOR_BLACK);
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf_col(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf_k("  [i] %s\n", buf);
}

void ui_print_success(const char *text) {
    vga_set_color(UI_COLOR_SUCCESS, COLOR_BLACK);
    printf_k("  [OK] %s\n", text);
}

void ui_print_error(const char *text) {
    vga_set_color(UI_COLOR_ERROR, COLOR_BLACK);
    printf_k("  [ERR] %s\n", text);
}

void ui_print_warning(const char *text) {
    vga_set_color(COLOR_YELLOW, COLOR_BLACK);
    printf_k("  [!] %s\n", text);
}

// ========== ENHANCED COMMAND FUNCTIONS ==========
void cmd_ls(void) {
    ui_print_header("FILESYSTEM");
    /* Use existing fs_list() helper to print files; it will handle empty dirs */
    if (fs_list() != 0) {
        ui_print_info("Directory is empty or filesystem not mounted");
    }
    ui_print_footer();
}

void cmd_cat(const char *name) {
    ui_print_header("VIEW FILE");
    
    if (!name || name[0] == 0) {
        ui_print_error("No filename specified");
        ui_print_footer();
        return;
    }
    
    char tmp[4096];
    int n = fs_read_file(name, tmp, sizeof(tmp)-1);
    if (n <= 0) { 
        ui_print_error("File not found or empty");
        ui_print_footer();
        return;
    }
    
    tmp[n] = 0;
    
    // Display file with line numbers
    vga_set_color(UI_COLOR_TEXT, COLOR_BLACK);
    printf_k("\n");
    
    int line_num = 1;
    char *start = tmp;
    char *ptr = tmp;
    
     while (*ptr) {
            if (*ptr == '\n') {
            // sPrint line number
            vga_set_color(COLOR_LIGHT_BLUE, COLOR_BLACK);
            printf_k("%d | ", line_num++);
            
            // Print the line content
            vga_set_color(UI_COLOR_TEXT, COLOR_BLACK);
            char saved = *(ptr);
            *(ptr) = 0;
            printf_k("%s\n", start);
            *(ptr) = saved;
            
            start = ptr + 1;
        }
        ptr++;
    }
    
    // Print last line
    if (start < ptr) {
        vga_set_color(COLOR_LIGHT_BLUE, COLOR_BLACK);
        printf_k("%d │ ", line_num);
        vga_set_color(UI_COLOR_TEXT, COLOR_BLACK);
        printf_k("%s\n", start);
    }
    
    ui_print_info("File size: %d bytes, %d lines", n, line_num);
    ui_print_footer();
}

void cmd_rm(const char *name) {
    ui_print_header("REMOVE FILE");
    
    if (!name || name[0] == 0) {
        ui_print_error("No filename specified");
        ui_print_footer();
        return;
    }
    
    printf_k("  Are you sure you want to delete '%s'? (y/n): ", name);
    
    char confirm[4];
    readline(confirm, sizeof(confirm));
    
    if (confirm[0] == 'y' || confirm[0] == 'Y') {
        if (fs_remove(name) == 0) {
            ui_print_success("File removed successfully");
        } else {
            ui_print_error("Failed to remove file");
        }
    } else {
        ui_print_info("Operation cancelled");
    }
    
    ui_print_footer();
}

void cmd_run(const char *name) {
    ui_print_header("EXECUTE PROGRAM");
    
    if (!name || name[0] == 0) { 
        ui_print_error("No program specified");
        ui_print_footer();
        return;
    }
    
    ui_print_info("Executing: %s", name);
    ui_print_divider('-');
    
    if (fs_run(name) == 0) {
        ui_print_divider('-');
        ui_print_success("Program completed");
    } else {
        ui_print_divider('-');
        ui_print_error("Failed to run program");
    }
    
    ui_print_footer();
}

void cmd_write(const char *name) {
    ui_print_header("CREATE FILE");
    
    if (!name || name[0] == 0) {
        ui_print_error("No filename specified");
        ui_print_footer();
        return;
    }
    
    vga_set_color(UI_COLOR_TEXT, COLOR_BLACK);
    printf_k("  Creating new file: %s\n", name);
    printf_k("  Type your content below. To finish, enter '.' on a single line:\n");
    printf_k("  -------------------------------------------------------------\n");
    
    char line[512];
    char buf[4096];
    int off = 0;
    int lines = 0;
    
    while (1) {
        // Show line number prompt
        vga_set_color(COLOR_LIGHT_BLUE, COLOR_BLACK);
        printf_k(" %3d │ ", lines + 1);
        vga_set_color(UI_COLOR_TEXT, COLOR_BLACK);
        
        readline(line, sizeof(line));
        
        if (line[0] == '.' && line[1] == 0) break;
        
        // Copy line
        int l = 0;
        while (line[l]) {
            if (off >= (int)sizeof(buf)-1) {
                ui_print_warning("File size limit reached (4KB)");
                break;
            }
            buf[off++] = line[l++];
        }
        
        if (off >= (int)sizeof(buf)-1) break;
        
        buf[off++] = '\n';
        lines++;
    }
    
    buf[off] = 0;
    
    if (fs_write_file(name, buf, off) == 0) {
        ui_print_success("File saved successfully");
        ui_print_info("Size: %d bytes, Lines: %d", off, lines);
    } else {
        ui_print_error("Failed to save file");
    }
    
    ui_print_footer();
}

void cmd_help(void) {
    ui_print_header("COMMAND HELP");
    
    vga_set_color(UI_COLOR_HIGHLIGHT, COLOR_BLACK);
    printf_k("  System Commands:\n");
    vga_set_color(UI_COLOR_TEXT, COLOR_BLACK);
    printf_k("    clear    - Clear the terminal screen\n");
    printf_k("    help     - Display this help message\n");
    printf_k("    exit     - Exit the shell (not implemented yet)\n\n");
    
    vga_set_color(UI_COLOR_HIGHLIGHT, COLOR_BLACK);
    printf_k("  File Operations:\n");
    vga_set_color(UI_COLOR_TEXT, COLOR_BLACK);
    printf_k("    ls       - List files in current directory\n");
    printf_k("    dir      - Alias for ls\n");
    printf_k("    cat <f>  - Display file contents\n");
    printf_k("    write <f>- Create/edit a text file\n");
    printf_k("    rm <f>   - Remove a file (with confirmation)\n");
    printf_k("    run <f>   - Execute a program\n\n");
    
    vga_set_color(UI_COLOR_HIGHLIGHT, COLOR_BLACK);
    printf_k("  Applications:\n");
    vga_set_color(UI_COLOR_TEXT, COLOR_BLACK);
    printf_k("    tetris   - Play Tetris game\n\n");
    
    ui_print_info("Use TAB for auto-completion (if implemented)");
    ui_print_info("Press CTRL+C to interrupt current operation");
    
    ui_print_footer();
}

void cmd_sysinfo(void) {
    ui_print_header("SYSTEM INFORMATION");
    
    // Get cursor position for dynamic display
    int x, y;
    vga_get_cursor(&x, &y);
    
    vga_set_color(UI_COLOR_TEXT, COLOR_BLACK);
    printf_k("  OS Name:         Binod OS\n");
    printf_k("  Version:         1.0.0\n");
    printf_k("  Terminal Size:   80x25\n");
    printf_k("  Cursor Position: %d,%d\n", x, y);
    printf_k("  Filesystem:      FAT-like\n");
    printf_k("  Memory:          ~640KB available\n");
    printf_k("  Processor:       386+ compatible\n");
    
    ui_print_footer();
}

// ========== ENHANCED CLI LOOP ==========
void cli_loop() {
    char line[256];
    int command_count = 0;
    
    // Print initial help
  //  cmd_help();
    
    while (1) {
        // Fancy prompt with command counter
    vga_set_color(UI_COLOR_PROMPT, COLOR_BLACK);
    printf_k("[binod@os");
        
    vga_set_color(COLOR_WHITE, COLOR_BLACK);
    printf_k(":%d", command_count++);
        
    vga_set_color(UI_COLOR_PROMPT, COLOR_BLACK);
    printf_k("] > ");
        
        // Get input
        readline(line, sizeof(line));
        
        // Handle empty input
        if (line[0] == 0) continue;
        
        // Handle clear command
        if (kstrncmp(line, "clear", 5) == 0) { 
            vga_clear(); 
            continue;
        }
        
        // Handle exit
        if (kstrncmp(line, "exit", 4) == 0) {
            ui_print_header("SHUTDOWN");
            ui_print_info("System shutting down...");
            ui_print_info("It is now safe to turn off your computer");
            ui_print_footer();
            while(1) asm("hlt");  // Halt CPU
        }
        
        // Handle commands with better parsing
        char *cmd = line;
        while (*cmd == ' ') cmd++;  // Skip leading spaces
        
        if (kstrncmp(cmd, "ls", 2) == 0 || kstrncmp(cmd, "dir", 3) == 0) { 
            cmd_ls(); 
            continue; 
        }
        
        if (kstrncmp(cmd, "tetris", 6) == 0) { 
            ui_print_header("TETRIS GAME");
            ui_print_info("Starting Tetris... Press 'q' to quit");
            ui_print_divider('-');
            tetris(); 
            vga_clear();
            continue;
        }
        
        if (kstrncmp(cmd, "cat ", 4) == 0) { 
            cmd_cat(cmd + 4); 
            continue; 
        }
        
        if (kstrncmp(cmd, "rm ", 3) == 0) { 
            cmd_rm(cmd + 3); 
            continue; 
        }
        
        if (kstrncmp(cmd, "write ", 6) == 0) { 
            cmd_write(cmd + 6); 
            continue; 
        }
        
        if (kstrncmp(cmd, "run ", 4) == 0) { 
            cmd_run(cmd + 4); 
            continue; 
        }
        if (kstrncmp(cmd, "bmp ", 4) == 0) {
            /* draw BMP at top-lefnt */
            const char *fname = cmd + 4;
            if (!fname || fname[0] == '\0') {
                ui_print_error("Usage: bmp <filename>");
                continue;
            }
            int rc ;
            //bmp_draw(fname, 0, 0);
            if (rc == 0) {
                ui_print_success("Image drawn");
            } else {
                ui_print_error("Failed to draw BMP");
                ui_print_info("Make sure '%s' exists on disk and is a supported BMP (24/32/8bpp)", fname);
            }
            continue;
        }
        if (kstrncmp(cmd, "bmp13 ", 6) == 0) {
            const char *fname = cmd + 6;
            if (!fname || fname[0] == '\0') { ui_print_error("Usage: bmp13 <filename>"); continue; }
           // vga_set_mode13();
            //vga_set_palette_default();
           int rc;
            //int rc = bmp_draw_mode13_auto(fname);
            if (rc == 0) ui_print_success("Mode13 image drawn"); else ui_print_error("Failed to draw mode13 BMP");
            continue;
        }
        
        if (kstrncmp(cmd, "help", 4) == 0) {
            cmd_help();
            continue;
        }
        
        if (kstrncmp(cmd, "sysinfo", 7) == 0 || kstrncmp(cmd, "info", 4) == 0) {
            cmd_sysinfo();
            continue;
        }
        
        // Command not found
        ui_print_header("ERROR");
        ui_print_error("Unknown command");
        ui_print_info("Type 'help' for available commands");
        ui_print_footer();
    }
}

// ========== ENHANCED KERNEL MAIN ==========
void kernel_main(uint32_t magic, uint32_t addr) {
    // Initialize with black background
    vga_set_color(UI_COLOR_TEXT, COLOR_BLACK);
    vga_clear();
    /* Initialize framebuffer (if the bootloader provided one via Multiboot) */
    fb_init(magic, addr);
    char fbmsg[64]; fb_status(fbmsg, sizeof(fbmsg));
    vga_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    printf_k("  %s\n", fbmsg);
    
    // Show animated boot sequence
    ui_print_banner();
    
    vga_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);

    for(int i = 0; i < 3; i++) {
        putc_k('.');
        // Small delay (you might need to implement delay function)
        for(int j = 0; j < 1000000; j++) asm("nop");
    }
    putc_k('\n');
    
    // Initialize subsystems
    ui_print_info("Loading ATA driver...");
    ata_init();
    
    ui_print_info("Setting up interrupts...");
    idt_init();
    
    ui_print_info("Mounting filesystem...");
    if (fs_init() != 0) {
        ui_print_error("Filesystem not found!");
        ui_print_info("Please run mkfs on disk image first");
    } else {
        ui_print_success("Filesystem mounted successfully");
        
        /* Show quick file count using fs_count_files() */
        int file_count = fs_count_files();
        ui_print_info("%d files found in root directory", file_count);
    }
    
    // Display logo if  dexists
    char logo_buffer[2048];
    if (fs_read_file("logo.txt", logo_buffer, sizeof(logo_buffer)-1) > 0) {
        ui_print_divider('=');
        logo_buffer[sizeof(logo_buffer)-1] = 0;
        
        // Display logo in a different color
        vga_set_color(COLOR_CYAN, COLOR_BLACK);
        printf_k("%s\n", logo_buffer);
        ui_print_divider('=');
    }
    
    // Boot complete message
    vga_set_color(COLOR_GREEN, COLOR_BLACK);
    printf_k("\n  System ready. Type 'help' to begin.\n\n");
    
    // Reset to normal text color
    vga_set_color(UI_COLOR_TEXT, COLOR_BLACK);
    
    // Start CLI
/*  for(int i =0;i<15;i++){
        vga_set_color(0,i);
        printf_k(" \n");
    }*/
    cli_loop();
}
