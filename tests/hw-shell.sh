#!/usr/bin/env bash
set -euo pipefail
mkdir -p evidence
python3 - "$@" <<'PY'
import os, pathlib, re, select, signal, subprocess, sys, tempfile, time, pty
shell = str(pathlib.Path(sys.argv[1]).resolve())
count = 0
with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp)
    def run(script, expected=None, env=None):
        global count
        p = subprocess.run([shell], input=script, text=True, capture_output=True,
                           cwd=root, env=env, timeout=20)
        assert p.returncode == 0, (script, p.returncode, p.stderr)
        assert 'AddressSanitizer' not in p.stderr and 'runtime error:' not in p.stderr, p.stderr
        if expected is not None: assert p.stdout == expected, (script, p.stdout, expected, p.stderr)
        count += 1
        return p
    run('\n   \npwd\nexit\n', str(root)+'\n')
    (root/'sub').mkdir()
    run('cd sub\npwd\ncd ..\npwd\n', str(root/'sub')+'\n'+str(root)+'\n')
    run('cd /no-such-cs162-directory\npwd\n', str(root)+'\n')
    run('cd\ncd a b\npwd\n', str(root)+'\n')
    run('/bin/echo "hello world" \'two words\' escaped\\ word\n', 'hello world two words escaped word\n')
    run('/bin/false\n/bin/echo after\n', 'after\n')
    run('nonexistent-cs162-command\n/bin/echo alive\n', 'alive\n')
    for path in ['/bin:/usr/bin', ':/bin', '/not-here::/bin', '/bin:']:
        run('echo resolved\n', 'resolved\n', dict(os.environ, PATH=path))
    tool = root/'localtool'
    tool.write_text('#!/bin/sh\necho local\n'); tool.chmod(0o755)
    run('localtool\n', 'local\n', dict(os.environ, PATH='/missing::/bin'))
    run('./localtool\n', 'local\n', dict(os.environ, PATH='/missing'))
    (root/'blocked').write_text('no permission')
    run('blocked\necho continued\n', 'continued\n', dict(os.environ, PATH=f'{root}:/bin'))
    run('echo first > output\necho x > output\ncat < output\n', 'x\n')
    run('echo "i love pintos and cs162" > input\nwc < input > output\ncat output\n')
    assert (root/'output').read_text().split() == ['1','5','24']
    run('cat < missing\necho survived\n', 'survived\n')
    run('echo nope > /no-such-cs162-directory/out\necho survived\n', 'survived\n')
    run('seq 1 100000 | cat | cat | wc -l\n', '100000\n')
    run('yes | head -n 1\n', 'y\n')
    run('seq 1 100 | grep 0 | wc -l > output\ncat < output\n', '10\n')
    run('cat < input | tr a-z A-Z > output\ncat output\n', 'I LOVE PINTOS AND CS162\n')
    run('echo many' + ' | cat'*40 + '\n', 'many\n')
    run(('echo repeat | cat | wc -c\n')*100, '7\n'*100)
    for bad in ['| echo bad', 'echo bad |', 'echo bad >', 'echo bad < | cat', '&', 'echo & bad']:
        p = run(bad+'\necho recovered\n', 'recovered\n')
        assert 'invalid command' in p.stderr
    # Background launch must return before a slow predecessor, then wait must join it.
    run("/bin/sh -c 'sleep 0.3; echo slow >> ordering' &\n/bin/sh -c 'echo fast >> ordering'\nwait\ncat ordering\n", 'fast\nslow\n')
    run('sleep 0.1 &\nsleep 0.2 &\nwait\necho joined\n', 'joined\n')
    run("/bin/sh -c 'echo $$ > stopped-pid; kill -STOP $$; echo done > stopped-result' &\n/bin/sh -c 'while test ! -f stopped-pid; do sleep 0.01; done; sleep 0.1; kill -CONT $(cat stopped-pid)' &\nwait\ncat stopped-result\n", 'done\n')
    run('fg 999999\nbg invalid\necho alive\n', 'alive\n')
    # Original bounded tokenizer: long input is rejected, next command still works.
    run('x'*5000+'\necho recovered\n', 'recovered\n')
print(f'Noninteractive cases passed: {count}', flush=True)
if '--pty' not in sys.argv: sys.exit(0)
# A real controlling terminal is necessary for process-group signals and job control.
pid, fd = pty.fork()
if pid == 0:
    os.execv(shell, [shell])
transcript = bytearray()
def receive(pattern=rb'\d+: ', timeout=10):
    data = bytearray(); deadline = time.monotonic()+timeout
    while time.monotonic() < deadline:
        if select.select([fd], [], [], min(0.1, max(0, deadline-time.monotonic())))[0]:
            try: chunk = os.read(fd, 65536)
            except OSError: break
            if not chunk: break
            data.extend(chunk); transcript.extend(chunk)
            if re.search(pattern, data): return bytes(data)
    raise AssertionError(('PTY timeout', pattern, bytes(data)))
def send(s): os.write(fd, s if isinstance(s, bytes) else s.encode())
checks = 0
try:
    receive()
    # Interrupting the prompt must preserve the shell.
    send(b'\x03'); send('echo prompt-survived\n'); receive(rb'prompt-survived\r\n\d+: '); checks+=1
    send('sleep 30\n'); time.sleep(.15); send(b'\x03'); receive(); checks+=1
    send('sleep 30 | cat\n'); time.sleep(.15); send(b'\x03'); receive(); checks+=1
    # Stopped foreground job returns terminal; fg resumes it and Ctrl-C kills it.
    send('sleep 30\n'); time.sleep(.15); send(b'\x1a'); receive(); checks+=1
    send('bg\n'); receive(); send('fg\n'); time.sleep(.15); send(b'\x03'); receive(); checks+=1
    # An immediate foreground reader must not get SIGTTIN before terminal handoff.
    for i in range(8):
        send('cat\n'); send(f'input-{i}\n'); receive(f'input-{i}\\r\\n'.encode());
        send(b'\x04'); receive(); checks+=1
    # A background terminal reader stops; fg gives it the terminal and resumes it.
    send('cat &\n'); receive(); time.sleep(.15)
    send('fg\n'); time.sleep(.15); send('resumed-reader\n'); receive(rb'resumed-reader\r\n');
    send(b'\x04'); receive(); checks+=1
    # Job termios must be restored when fg resumes, shell termios at each prompt.
    send("/bin/sh -c 'stty -echo; kill -STOP $$; stty -a; stty echo'\n")
    receive(); send('fg\n'); out = receive();
    assert re.search(rb'(?:^|[ ;])-echo(?:[ ;\r\n])', out), out
    checks+=1
    # Explicit member PID selection, both bg and fg, on a stopped job.
    send("/bin/sh -c 'echo BG_PID=$$; kill -STOP $$; exec sleep 30' &\n")
    out = receive(rb'BG_PID=(\d+)\r\n'); child = re.search(rb'BG_PID=(\d+)\r\n', out).group(1)
    time.sleep(.15); send(b'bg '+child+b'\n'); receive()
    send(b'fg '+child+b'\n'); time.sleep(.15); send(b'\x03'); receive(); checks+=1
    send(str(pathlib.Path(shell).parent/'terminal-check')+'\n'); receive(rb'terminal-check-ok\r\n\d+: '); checks+=1
    send(b'\x1a'); send(b'\x1c'); send('echo signals-survived\n'); receive(rb'signals-survived\r\n\d+: '); checks+=1
    # Nested shell receives terminal signals independently and returns ownership.
    send(shell+'\n'); receive(); send(b'\x03'); send('echo nested-alive\n'); receive(rb'nested-alive\r\n\d+: ')
    send('exit\n'); receive(); send('echo parent-alive\n'); receive(rb'parent-alive\r\n\d+: '); checks+=1
    send('exit\n')
    deadline = time.monotonic()+10
    while time.monotonic()<deadline:
        got,status = os.waitpid(pid, os.WNOHANG)
        if got:
            assert os.waitstatus_to_exitcode(status)==0, status
            break
        time.sleep(.05)
    else: raise AssertionError('shell failed to exit')
    print(f'PTY checks passed: {checks}')
finally:
    pathlib.Path('evidence/pty-transcript.txt').write_bytes(transcript)
    try: os.kill(pid, signal.SIGKILL)
    except ProcessLookupError: pass
    os.close(fd)
PY
