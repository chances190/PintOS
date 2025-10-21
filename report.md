# PintOS Projeto 1 - Relatório de Implementação

## Parte 1: Thread Sleep (Alarm Clock)

### Resumo
Implementação de um mecanismo de bloqueio de thread e alarme para substituir a abordagem de busy-wait em `timer_sleep()`.

### `src/threads/thread.h` e `src/threads/thread.c`
- Adicionado campo `int64_t wake_time` ao `struct thread`, para armazenar a contagem absoluta de ticks de quando uma thread adormecida deve acordar
- Modificado `init_thread()` para inicializar o campo `wake_time` em 0

### `src/devices/timer.c`
- Adicionado include para `lib/kernel/list.h`
- Adicionado `sleep_list` estático para rastrear threads adormecidas
- Implementada função de comparação de lista `wake_time_less()`
- Modificado `timer_init()` para inicializar a lista de sono
- Reescrito `timer_sleep()` para usar bloqueio de threads
- Modificado `timer_interrupt()` para acordar threads adormecidas

### Design

**Thread Sleep:**
1. Thread chama `timer_sleep(ticks)`
2. Calcula `wake_time = current_ticks + ticks`
3. Desativa interrupções (seção crítica)
4. Insere thread em `sleep_list` ordenada por `wake_time`
5. Bloqueia a thread (remove da fila pronta, define status como BLOCKED)
6. Reativa interrupções
7. Escalonador executa próxima thread pronta

**Thread Wake-Up:**
1. Interrupção do timer dispara (a cada tick)
2. Incrementa contador global `ticks`
3. Verifica início de `sleep_list`
4. Enquanto threads existem com `wake_time <= current_ticks`:
   - Remove thread de `sleep_list`
   - Desbloqueia thread (adiciona à fila pronta, define status como READY)
5. Para ao encontrar thread com `wake_time` futuro

### Resultados de Testes

Todos os testes de relógio de alarme passaram:
- ✅ `alarm-single`: Funcionalidade básica de sono
- ✅ `alarm-multiple`: Múltiplas threads dormindo
- ✅ `alarm-simultaneous`: Threads acordando no mesmo tempo
- ✅ `alarm-priority`: Sono não afeta escalonamento de prioridade
- ✅ `alarm-zero`: Dormir por 0 ticks (caso extremo)
- ✅ `alarm-negative`: Dormir por ticks negativos (caso extremo)

---
### Modificações nos ficheiros

### `src/threads/thread.c` e `src/threads/thread.h`
- Implementada função de comparação de lista `thread_priority_less()`
- Modificados `thread_unblock()` e `thread_yield()` para inserir threads em ordem
- Acrescentadoas verificações de preempção de prioridade em `thread_create()` e `thread_set_priority()` 

###  `src/threads/synch.c`
- Implementada função de comparação de lista `cond_priority_less()`
- Reescrito `sema_up()` para ordenar `sema->waiters` por prioridade e verificar preempção de prioridade
- Reescrito `cond_signal()` para ordenar `cond->waiters` por prioridade e sinalizar o elemento de maior prioridade


### Design 

**Criação de thread**
1. `thread_create()` inicializa a nova thread e chama `thread_unblock(t)` para colocá‑la na `ready_list`
2. `thread_unblock()` insere thread na `ready_list` em ordem por prioridade
3. `thread_create()` verifica se a nova thread tem prioridade maior que a thread atual; se sim, a thread atual chama `thread_yield()` para preempção.

**Escalonamento**
1. `thread_yield()` insere a thread atual de volta na `ready_list` em ordem por prioridade (O(n)) e chama o escalonador para escolher a próxima execução
2. Escalonador escolhe primeira thread da `ready_list`. Como a lista é ordenada, a seleção é O(1)

**Sincronização**
1. Ao chamar `sema_down()`/`lock_acquire()`/`cond_wait()` a thread é removida da `ready_list` e colocada no fim da lista de waiters correspondente.
2. `cond_signal()` ordena `cond->waiters` por prioridade comparando o primeiro waiter de cada `semaphore_elem` e sinaliza o que contém o waiter de maior prioridade.
3. Em `sema_up()` o código ordena `sema->waiters` por prioridade no momento do desbloqueio , selecionando a thread com maior prioridade atual.
4. Após desbloquear, verifica-se se a nova thread tem prioridade maior que a thread atual; se sim, a thread atual chama `thread_yieald()` para preempção


**Mudança de prioridade**
1. `thread_set_priority()` atualiza a prioridade, e em seguida verifica se a nova thread tem prioridade maior que a thread atual; se sim, a thread atual chama `thread_yield()` para permitir preempção.

### Testes
Todos os testes de preempção passaram. Os testes de doação de prioridade falharam porque a doação ainda não foi implementada:

- ✅ `priority-change`: Mudança explícita de prioridade reflete no escalonador imediatamente
- ✅ `priority-fifo`: Threads com mesma prioridade respeitam ordem FIFO
- ✅ `priority-preempt`: Criação/alteração de ready de maior prioridade causa preempção imediata
- ✅ `priority-sema`: Semáforo acorda a thread de maior prioridade disponível
- ✅ `priority-condvar`: Condvar sinaliza o waiter de maior prioridade
- ❌ `priority-donate-one`: Doação simples (um nível)
- ❌ `priority-donate-multiple`: Doação envolvendo múltiplos waiters
- ❌ `priority-donate-multiple2`: Variante de cenário múltiplo
- ❌ `priority-donate-nest`: Doação aninhada / propagação transitiva
- ❌ `priority-donate-sema`: Doação em casos com semáforos
- ❌ `priority-donate-lower`: Casos onde o holder tem prioridade menor
- ❌ `priority-donate-chain`: Cadeia de doações transitivas

### Próximos passos recomendados

1. Adicionar campos em `struct thread` para suportar doação: `base_priority`, `locks_held` e `lock_waiting_on`.
2. Implementar doação de prioridade em `lock_acquire()` (doar ao holder e propagar transitivamente).
3. Atualizar `lock_release()` para remover doações associadas e recalcular a prioridade efetiva.
4. Ajustar `thread_get_priority()`/`thread_set_priority()` para considerar doações e base_priority.

Com essas mudanças a implementação passará os testes de doação e evitará inversão de prioridade em casos com cadeias de locks.

---
