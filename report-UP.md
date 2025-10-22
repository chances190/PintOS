# PintOS - Relatório de Implementação

## Projeto 2 - User Programs

### 1. Argument Passing
1.1 userprog/process.c → setup_stack()  
    1.1.1 Localizar esp = PHYS_BASE  
    1.1.2 Aplicar hack inicial: `*esp = PHYS_BASE - 12`  
    1.1.3 Fazer cópia do `file_name` antes de modificá-lo (strtok_r() destrói a string)  
1.2 Parser completo em setup_stack()  
    1.2.1 Tokenizar `file_name` usando `strtok_r()` com delimitador espaço  
    1.2.2 Tratar múltiplos espaços como um (strtok_r() já faz isso)  
    1.2.3 Impor limite razoável: argumentos devem caber em uma página (4 KB)  
    1.2.4 Contar `argc` e armazenar cada argumento em vetor local  
    1.2.5 Alinhar `esp` a múltiplos de 4 bytes (word-align)  
    1.2.6 Copiar strings da ordem inversa para a pilha (decremento de `esp`)  
    1.2.7 Empilhar null pointer terminador (para compatibilidade)  
    1.2.8 Empilhar ponteiros `argv[i]` em ordem reversa  
    1.2.9 Empilhar ponteiro para `argv`, valor de `argc` e endereço de retorno fake (0)

### 2. Acesso à Memória do Usuário
2.1 Escolher método de validação  
    2.1.1 Opção A: verificar validade antes de deferenciar (mais seguro, mais lento)  
    2.1.2 Opção B: deferenciar e capturar page faults (mais rápido, requer tratamento em exception.c)  
2.2 userprog/syscall.c → Funções de acesso seguro (Opção B recomendada)  
    2.2.1 Implementar `get_user(const uint8_t *uaddr)` via assembly (retorna -1 se segfault)  
    2.2.2 Implementar `put_user(uint8_t *udst, uint8_t byte)` via assembly (retorna false se segfault)  
    2.2.3 Função `is_user_vaddr(const void *vaddr)` comparando contra `PHYS_BASE`  
2.3 userprog/exception.c → Modificar page_fault()  
    2.3.1 Se falta for em endereço de usuário (is_user_vaddr()), setar `eax = 0xffffffff`  
    2.3.2 Copiar valor antigo de `eip` para `eax` antes de avançar `eip` para next instruction  
    2.4 userprog/syscall.c → Helpers de cópia segura  
    2.4.1 `check_address(const void *addr)` valida e mata o processo se inválido  
    2.4.2 `safe_copy_in(void *dst, const void *src, size_t size)` copia de userspace  
    2.4.3 `safe_copy_out(void *dst, const void *src, size_t size)` copia para userspace

### 3. Infraestrutura de System Calls
3.1 userprog/syscall.c → Inicialização  
    3.1.1 `syscall_init()`: registrar interrupção 0x30 com `intr_register_int()`  
    3.1.2 Usar `intr_new()`/`intr_set_level()` para configurar nível de privilégio  
3.2 userprog/syscall.c → Handler principal  
    3.2.1 Ler nº da syscall de `*(int *)f->esp` (primeiro argumento na pilha)  
    3.2.2 Validar que `f->esp` é endereço de usuário com `check_address()`  
    3.2.3 `switch` baseado no syscall number (definições em lib/syscall-nr.h)  
    3.2.4 Extrair argumentos da pilha do usuário (próximas posições em esp+4, esp+8, etc.)  
    3.2.5 Chamar handler apropriado e armazenar retorno em `f->eax`

### 4. Implementação de Syscalls Básicas
4.1 halt (SYS_HALT)  
    - userprog/syscall.c → `sys_halt()`  
    - chamar `shutdown_power_off()` (devices/shutdown.h)  
    - não retorna; encerra Pintos completamente  
4.2 exit (SYS_EXIT)  
    - userprog/syscall.c → `sys_exit(int status)`  
    - extrair `status` da pilha do usuário via `check_address()`  
    - imprimir mensagem: `printf("%s: exit(%d)\n", thread_name(), status)`  
    - armazenar `status` em campo da thread para wait() recuperar  
    - chamar `thread_exit()`  
4.3 write (SYS_WRITE)  
    - userprog/syscall.c → `sys_write(int fd, const void *buffer, unsigned size)`  
    - suportar somente `fd == 1` (STDOUT_FILENO) inicialmente  
    - validar `buffer` com `check_address()` para todos os `size` bytes  
    - para fd == 1: chamar `putbuf(buffer, size)` em uma única chamada  
    - retornar número de bytes escritos (ou -1 se erro)  
4.4 Stubs iniciais para syscalls de arquivo  
    - open (SYS_OPEN) → retornar -1  
    - close (SYS_CLOSE) → retornar sucesso  
    - read (SYS_READ) → retornar -1  
    - filesize (SYS_FILESIZE) → retornar -1  
    - seek (SYS_SEEK) → sem retorno  
    - tell (SYS_TELL) → retornar 0  
    - create (SYS_CREATE) → retornar false  
    - remove (SYS_REMOVE) → retornar false  

### 5. Espera por Processo Filho
5.1 userprog/process.c → `process_wait(tid_t child_tid)`  
    5.1.1 Implementação inicial: transformar em `while (true);` (loop infinito)  
    5.1.2 Garante que Pintos não desligue antes dos processos rodarem  
5.2 Versão completa  
    5.2.1 Criar `struct child_info` com campos: `tid`, `exit_status`, `semaphore sema_wait`, `list_elem elem`  
    5.2.2 Manter lista de filhos em `struct thread`  
    5.2.3 Em `process_execute()`, registrar filho na lista e copiar seu tid  
    5.2.4 Em `process_wait()`: procurar filho na lista, fazer `sema_down()` até ele terminar  
    5.2.5 Em `process_exit()`: procurar pai na lista, chamar `sema_up()` para acordar pai  
    5.2.6 Retornar -1 se pid não é filho direto ou se já foi aguardado  
5.3 exec (SYS_EXEC)  
    - userprog/syscall.c → `sys_exec(const char *cmd_line)`  
    - validar `cmd_line` pointer com `check_address()`  
    - chamar `process_execute(cmd_line)`  
    - retornar pid do novo processo ou -1 se falhar  
    - **importante**: sincronizar com filho para garantir que executável foi carregado  
5.4 wait (SYS_WAIT)  
    - userprog/syscall.c → `sys_wait(pid_t pid)`  
    - chamar `process_wait(pid)` e retornar status  

### 6. Negar Escrita em Executáveis
6.1 userprog/process.c → `load()` e `process_execute()`  
    6.1.1 Após carregar arquivo executável com sucesso, chamar `file_deny_write(file)`  
    6.1.2 Manter referência aberta ao arquivo enquanto processo está rodando  
6.2 userprog/process.c → `process_exit()`  
    6.2.1 Ao finalizar processo, chamar `file_allow_write(executable_file)`  
    6.2.2 Fechar o arquivo com `file_close()`  
6.3 userprog/syscall.c → `sys_write()`  
    6.3.1 Verificar se arquivo aberto em fd é o executável do processo  
    6.3.2 Se for, retornar 0 (negar escrita)  
6.4 Testes: programas que tentam sobrescrever seu próprio binário devem falhar silenciosamente

### 7. Sincronização de System Calls
7.1 userprog/syscall.c  
    7.1.1 Adicionar `static struct lock filesys_lock`  
    7.1.2 Em `syscall_init()`: inicializar `lock_init(&filesys_lock)`  
7.2 Proteção do filesystem  
    7.2.1 Envolver chamadas a `filesys/` (open, close, read, write, etc.) com `lock_acquire()` e `lock_release()`  
    7.2.2 **Importante**: process_execute() também acessa filesystem → também precisa de proteção  
7.3 Tratamento de exceções durante críticas  
    7.3.1 Se process termina enquanto holds lock, liberar antes de chamar thread_exit()

