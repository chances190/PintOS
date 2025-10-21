# PintOS Projeto 1 - Relatório de Implementação

## Parte 1: Thread Sleep (Alarm Clock)

### Resumo
Implementação de um mecanismo de bloqueio de thread e alarme para substituir a abordagem de busy-wait em `timer_sleep()`.

### 1. `src/threads/thread.h`
- Adicionado campo `int64_t wake_time` ao `struct thread`, para armazenar a contagem absoluta de ticks de quando uma thread adormecida deve acordar

### 2. `src/threads/thread.c`
- Modificado `init_thread()` para inicializar o campo `wake_time` em 0

### 3. `src/devices/timer.c`
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

## Parte 2: Escalonador com Prioridade (Pendente)

*A ser implementado*

Escalonamento com prioridade e doação incluirá:
- Ordenação de fila pronta baseada em prioridade
- Doação de prioridade para locks
- Tratamento de doação aninhada e múltipla
- Preempção por prioridade
- Atualizações para semáforos e variáveis de condição

---
