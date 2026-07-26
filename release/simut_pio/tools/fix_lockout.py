#!/usr/bin/env python3
"""Fix DisplayManager.cpp - reduce lockout timeout and add Core 1 restart."""
import re

path = '/home/angelo/Documentos/simut/src/DisplayManager.cpp'
with open(path) as f:
    content = f.read()

S = '\t\t\t\t'  # 4 tabs

# 1. Reduce timeout from 10s to 3s and add _core1HardReset flag
old = (S + '/* After 10s without success, fall back to hard reset.\n'
       + S + ' * multicore_reset_core1() stops Core 1 immediately - no\n'
       + S + ' * handshake needed. All flash ops are safe. */\n'
       + S + 'if (timeSince(retryStart, 10000)) {\n'
       + S + '\tSerial.println("[DSP] Lockout stuck >10s, hard reset Core1");\n'
       + S + '\t__atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);\n'
       + S + '\tLogManager::instance( ).setCorePaused(1, true);\n'
       + S + '\tmulticore_lockout_end_blocking( );\n'
       + S + '\tmulticore_reset_core1( );\n'
       + S + '\t_pauseStartTime = 0;\n'
       + S + '\t/* Core 1 dead - flash ops safe. */\n'
       + S + '\treturn;\n'
       + S + '}')

new = (S + '/* After 3s without success, fall back to hard reset.\n'
       + S + ' * multicore_reset_core1() stops Core 1 immediately - no\n'
       + S + ' * handshake needed. All flash ops are safe. */\n'
       + S + 'if (timeSince(retryStart, 3000)) {\n'
       + S + '\tSerial.println("[DSP] Lockout stuck >3s, hard reset Core1");\n'
       + S + '\t__atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);\n'
       + S + '\tLogManager::instance( ).setCorePaused(1, true);\n'
       + S + '\tmulticore_lockout_end_blocking( );\n'
       + S + '\tmulticore_reset_core1( );\n'
       + S + '\t_pauseStartTime = 0;\n'
       + S + '\t_core1HardReset = true;\n'
       + S + '\treturn;\n'
       + S + '}')

if old in content:
    content = content.replace(old, new, 1)
    print('Lockout timeout reduced from 10s to 3s + _core1HardReset')
else:
    print('ERROR: old lockout block not found')
    # Debug
    idx = content.find('After 10s without success')
    if idx >= 0:
        print(f'Found at position {idx}')
        print(repr(content[idx-20:idx+500]))

# 2. Add relaunch in unpause path
S2 = '\t\t'  # 2 tabs for inner content
old_unpause = ('\t} else {\n'
               + S2 + 'int32_t prev = __atomic_fetch_sub(&_pauseRefCount, 1, __ATOMIC_ACQ_REL);\n'
               + S2 + 'if (prev <= 1) {\n'
               + '\n'
               + S2 + '__atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);\n'
               + S2 + '_pauseStartTime = 0;\n'
               + S2 + 'multicore_lockout_end_blocking( );\n'
               + S2 + 'LogManager::instance( ).setCorePaused(1, false);\n'
               + S2 + '}\n'
               + '\t}')

new_unpause = ('\t} else {\n'
               + S2 + 'int32_t prev = __atomic_fetch_sub(&_pauseRefCount, 1, __ATOMIC_ACQ_REL);\n'
               + S2 + 'if (prev <= 1) {\n'
               + '\n'
               + S2 + '__atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);\n'
               + S2 + '_pauseStartTime = 0;\n'
               + S2 + 'multicore_lockout_end_blocking( );\n'
               + S2 + 'LogManager::instance( ).setCorePaused(1, false);\n'
               + S2 + '/* Restart Core 1 if it was hard-reset by lockout timeout */\n'
               + S2 + 'if (__atomic_exchange_n(&_core1HardReset, false, __ATOMIC_ACQ_REL)) {\n'
               + S2 + '\tmulticore_launch_core1(core1Entry);\n'
               + S2 + '}\n'
               + S2 + '}\n'
               + '\t}')

if old_unpause in content:
    content = content.replace(old_unpause, new_unpause, 1)
    print('Unpause path updated with Core 1 restart')
else:
    print('ERROR: unpause block not found')
    idx = content.find('else {')
    # Find the right 'else' (the unpause one)
    unpause_marker = content.find('__atomic_fetch_sub(&_pauseRefCount, 1')
    if unpause_marker >= 0:
        print(f'Found unpause at position {unpause_marker}')
        print(repr(content[unpause_marker-30:unpause_marker+350]))

with open(path, 'w') as f:
    f.write(content)
print('Done')
