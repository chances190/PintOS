# PintOS Development Guide for Coding Agents

## Project Overview

**PintOS** is an educational operating system project based on Stanford's CS140 curriculum. This is a C-based kernel implementation targeting x86 architecture, compiled for 32-bit execution. The project consists of 4 sequential phases:

1. **Threads & Scheduling** Alarm clock, priority scheduling, priority donation, MLFQS scheduler
2. **User Programs & System Calls** Argument passing, process control (exec/wait/exit), file operations, memory validation
3. **Virtual Memory**: Supplemental page table, lazy loading, stack growth, memory-mapped files, swap table
4. **File Systems**: Buffer cache, indexed/extensible files, subdirectories, synchronization

**Repository Stats**: ~50 source files, primarily C with assembly (.S) for bootstrapping. Validation is entirely manual via test suite (149 total tests across all phases).

## Critical Build Information

### Prerequisites
- **Required**: gcc (32-bit support), make, qemu-system-i386, gdb
- **Arch Linux Note**: Custom workaround in `src/Makefile.build:93-96` truncates `loader.bin` to 512 bytes (Arch-specific issue)
- **Compiler**: Uses gcc with `-m32 -march=i686 -msoft-float -fno-stack-protector`

### Build Process (IDENTICAL for all project phases)

**ALWAYS follow this exact sequence**:

1. **First-time setup** (do once per workspace):
   ```bash
   cd src/utils && make
   ```
   This creates executables needed by test infrastructure.

2. **Build any phase** (threads/userprog/vm/filesys):
   ```bash
   cd src/<phase>      # e.g., cd src/threads
   make clean          # Optional to ensure clean build
   make                # Creates build/ directory with kernel.bin and loader.bin
   ```
   
   Build takes ~10-30 seconds. Creates `build/` subdirectory containing:
   - `kernel.bin` (~100KB) - the compiled kernel
   - `loader.bin` (512 bytes) - bootloader
   - Test executables in `build/tests/<phase>/`

3. **Run tests**:
   ```bash
   cd src/<phase>
   make check          # Runs ALL tests, takes 60-480 seconds depending on phase
   ```

### Known Issues

1. **Arch Linux loader.bin bug**:  
   `src/Makefile.build:93-96` includes a `dd` command to truncate `loader.bin`.  
   **DO NOT REMOVE** — this is a required workaround.

2. **Expected warnings**:
   - `lib/debug.c`: Warning about `__builtin_frame_address` is expected
   - `squish-unix.c`: Format security warning is known and harmless
   - Tests may show "Can't exec backtrace" — this is expected and does not affect test results.

## Project Architecture

### Directory Structure

```
src/
├── threads/            # Phase 1: Kernel threads, scheduling, synchronization
│   ├── thread.{c,h}    # Core thread implementation, struct thread
│   ├── synch.{c,h}     # Locks, semaphores, condition variables
│   ├── init.c          # Kernel initialization entry point
│   └── Makefile        # Builds threads phase (TEST variable for specific tests)
├── userprog/           # Phase 2: User processes, system calls, argument passing
│   ├── process.{c,h}   # Process loading, stack setup, exec/wait
│   ├── syscall.{c,h}   # System call handler and implementations
│   ├── exception.{c,h} # Page faults, user memory access violations
│   └── Makefile        # Builds userprog phase
├── vm/                 # Phase 3: Virtual memory management
│   ├── (empty)         # Create: page.{c,h}, frame.{c,h}, swap.{c,h}
│   └── Make.vars       # Defines VM build with -DVM flag
├── filesys/            # Phase 4: File system with indexing, subdirectories, caching
│   ├── inode.{c,h}     # File metadata, extensible files
│   ├── file.{c,h}      # File operations
│   ├── directory.{c,h} # Directory operations, hierarchical namespace
│   ├── filesys.{c,h}   # Filesystem interface (formatting, mounting)
│   ├── free-map.{c,h}  # Sector allocation bitmap
│   └── Make.vars       # Can build with/without VM (see commented lines)
├── devices/            # Hardware abstraction
│   └── timer.{c,h}     # Timer interrupt, timer_sleep()
├── lib/                # Shared utilities
│   ├── kernel/         # Kernel-only: list, bitmap, hash, console
│   ├── user/           # User-space syscall wrappers
│   ├── debug.h         # Custom DEBUG() macro for conditional printing
│   └── syscall-nr.h    # System call number definitions
├── tests/              # Test infrastructure
│   ├── threads/        # Phase 1 (27 tests)
│   ├── userprog/       # Phase 2 (64 tests)
│   ├── vm/             # Phase 3 (113 tests)
│   ├── filesys/        # Phase 4 (125 tests)
│   └── Make.tests      # Common test execution logic
├── utils/              # Build utilities (MUST be built first)
│   ├── pintos          # Perl script to run tests in QEMU
│   └── Makefile        # Creates setitimer-helper, squish-unix
├── Makefile            # Top-level (only for clean targets)
├── Make.config         # Compiler/linker configuration (32-bit gcc setup)
├── Makefile.build      # Kernel build rules (includes Arch Linux loader.bin fix)
└── Makefile.userprog   # User program compilation rules
```

### Key Files by Phase

**Phase 1 - Threads**:
- `src/threads/thread.{c,h}` - Thread structure and lifecycle
- `src/threads/synch.{c,h}` - Synchronization primitives
- `src/devices/timer.{c,h}` - Timer functionality

**Phase 2 - User Programs**:
- `src/userprog/process.{c,h}` - Process management
- `src/userprog/syscall.{c,h}` - System call interface
- `src/userprog/exception.{c,h}` - Exception handling

**Phase 3 - Virtual Memory**:
- `src/vm/page.{c,h}` - Supplemental page table
- `src/vm/frame.{c,h}` - Physical frame management
- `src/vm/swap.{c,h}` - Swap space management

**Phase 4 - File Systems**:
- `src/filesys/inode.{c,h}` - Inode and block management
- `src/filesys/directory.{c,h}` - Directory operations
- `src/filesys/filesys.{c,h}` - File system interface

## Implementation Reports - MANDATORY

**CRITICAL**: After completing and testing changes, you MUST update the corresponding phase report file with detailed documentation in Portuguese (PT-BR).

### Report Files (in repository root)
- `report-TH.md` - Phase 1: Threads & Scheduling
- `report-UP.md` - Phase 2: User Programs & System Calls  
- `report-VM.md` - Phase 3: Virtual Memory
- `report-FS.md` - Phase 4: File Systems
- `test-results` - Test Results Summary

### Required Report Content (in PT-BR)

**IMPORTANT**: Reports must be written in Portuguese (PT-BR). The report structure is organized by FUNCTIONALITY (not by phase). Each functionality must be a separate section containing Modified Files, Design, and Test Results.

Each functionality section must contain:

1. Section Header
   - Format: `### Parte X: Feature Name`
   - Provide a short overview of what was implemented for this functionality.

2. Modified Files Subsections
   - Create one subsection per modified file title. You may group a .c and its corresponding .h under a single subsection but the subsection title must be the file path(s) (e.g., `#### src/userprog/process.c, src/userprog/process.h`).
   - For each subsection, use bullet points that start with clear action verbs (e.g., "Added", "Modified", "Removed", "Renamed", "Refactored", "Fixed", "Documented") to describe exactly what was changed.
   - Specify which functions, methods, macros, and struct fields were added, modified, or removed. Use the exact symbol names.
   - For each change, include the intent / purpose in a short clause (e.g., "Added function foo() to validate user pointers and prevent kernel crashes").
   - If a struct was extended, list added fields and state why they were added and how they are used.
   - Example layout:
     - `#### src/userprog/process.c, src/userprog/process.h`
       - Added: `process_execute()` — starts a user process with validated argv parsing.
       - Modified: `load()` — now supports lazy stack growth to avoid eager allocation.
       - Changed struct `process` — added `exit_status` (int) to store child exit codes for wait().

3. Design Subsection
   - Title: `#### Design`
   - Provide a holistic description of the implementation and the rationale behind major design decisions.
   - Include:
     - Overall architecture and data flow (how components interact).
     - Key algorithms and data structures used.
     - Important invariants, synchronization strategy, and failure modes considered.
     - Explicit design decisions and trade-offs (e.g., why choose eager vs lazy allocation, single lock vs fine-grained locks, data structure X over Y), and the impact of those decisions on correctness, performance, and testability.
     - Step-by-step flows for core operations, numbered (e.g., 1. on syscall entry -> 2. validate -> 3. lock -> 4. perform operation -> 5. unlock -> 6. return).
     - Any assumptions and limitations.
   - Be precise and justify why each major approach was chosen.

4. Test Results Subsection
   - Title: `#### Resultados de Testes`
   - List all tests relevant to this functionality only.
   - Use the format:
     - `- ✅/❌ test-name: Brief description of what the test validates`
   - If a test fails even tough the functionality was already implemented, include the failing output path (e.g., `build/tests/<phase>/<test>.output`) and a concise note about suspected cause.

Formatting and content rules (mandatory)
- Keep each Modified Files subsection focused: one title per subsection, action-verb bullets, exact symbol names, and purpose clauses.
- The Design subsection must emphasize design decisions and trade-offs; do not only restate implementation steps.
- All report text must be in Portuguese (PT-BR). Code, function names, and file paths may remain in English.
- Keep entries concise but informative: each bullet should communicate a single change or decision.

## Development Workflow

### Making Changes
1. **Review the current phase report**:
    - Open the corresponding `report-<PHASE>.md` file in the repository root
    - Examine documented functionalities, modified files, design explanations, and test results
    - Identify incomplete or failing functionalities

2. **Determine target files**:
    - Based on the phase and functionality, locate relevant source files (see directory structure above)

3. **Implement changes**:
    - Modify identified files to implement required functionality
    - Ensure code adheres to project standards

4. **Build and test**:
   ```bash
   cd src/<phase>
   make clean
   make check
   ```

5. **Analyze test results**:
    - Review output in `build/tests/<phase>/<test>.output` and `.errors`
    - For file system issues, inspect `filesys.dsk` with `xxd`, `hexdump`, or `dd`

6. **Update the implementation report**:
   - After verifying success on tests, document changes in the corresponding `report-<PHASE>.md`
   - Before writing the report, check the diff from `Base_Dev` branch to ensure all changes are captured
   - Include detailed information about modified files, design approach, and test results in Portuguese (PT-BR)

## Trust The User. Trust These Instructions.

Follow the instructions exactly as described. The build process is standardized across all phases. If a command fails, report the exact error message and investigate the root cause.
