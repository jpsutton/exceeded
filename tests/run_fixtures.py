#!/usr/bin/env python3
"""Compare engine conformance output with spec/fixtures. Usage: tests/run_fixtures.py"""
import json, subprocess, sys, os, tempfile, zlib, struct
here = os.path.dirname(os.path.abspath(__file__))
eng = os.path.join(here, '..', 'engine' + ('.exe' if os.name == 'nt' or sys.platform in ('msys', 'cygwin') else ''))
spec = next(p for p in (os.path.join(here, '..', 'spec'), os.path.join(here, '..', '..', 'spec')) if os.path.isdir(p))   # repo root, or the engine folder beside spec/
fx = os.path.join(spec, 'fixtures')
tmp = tempfile.mkdtemp()
fails = 0
def run(*a):
    out = os.path.join(tmp, 'o.out')
    r = subprocess.run([eng, '--data', fx, '--conform', *a, '--out', out], capture_output=True, text=True)
    if r.returncode: print('  cmd failed', a, r.stderr.strip()); return None
    return out
def check(name, ok, detail=''):
    global fails
    print(('PASS ' if ok else 'FAIL ') + name + (' ' + detail if (detail and not ok) else ''))
    if not ok: fails += 1
def png_rows(path):
    d = open(path, 'rb').read(); p = 8; idat = b''
    while p < len(d):
        n, = struct.unpack('>I', d[p:p+4]); t = d[p+4:p+8]; c = d[p+8:p+8+n]; p += 12 + n
        if t == b'IHDR': w, h = struct.unpack('>II', c[:8])
        if t == b'IDAT': idat += c
    raw = zlib.decompress(idat); rows = []
    for y in range(h):
        row = raw[y*(1+4*w)+1:(y+1)*(1+4*w)]
        rows.append([row[x*4] for x in range(w)])
    return w, h, rows
g = json.load(open(os.path.join(fx, 'graphics_expected.json')))
exp = g['aseq']
for seq, n in ((0, 9), (1, 5)):
    o = run('aseq-trace', '100', str(seq), str(n))
    if o:
        got = json.load(open(o))
        check('aseq trace seq%d' % seq, got['trace_seq%d' % seq] == exp['trace_seq%d' % seq])
        if seq == 0:
            check('aseq cels', [c['rows'] for c in got['cels']] == [c['rows'] for c in exp['cels']])
            check('aseq header', all(got[k] == exp[k] for k in ('nseq', 'ncel', 'fps', 'frame_period_ticks')))
o = run('text', '15001', 'ABCA')
if o:
    w, h, rows = png_rows(o)
    got = [''.join('#' if v == 0 else '.' for v in r) for r in rows]
    e = g['nfnt']['rows']
    check('nfnt text', got == e, '\n'.join(got) + ' (size %dx%d)' % (w, h))
o = run('list-resources')
if o:
    got = json.load(open(o))
    c = json.load(open(os.path.join(fx, 'container_expected.json')))
    fixf = [f for f in got['files'] if f['file'] == 'FIXTURE.DAT'][0]
    ok = fixf['align_shift'] == c['align_shift'] and [(r['type'], r['id']) for r in fixf['resources']] == [(r['type'] if isinstance(r['type'], str) else {15:'NAMETABLE'}.get(r['type'], r['type']), r['id']) for r in c['resources']]
    check('container', ok, str([(r['type'], r['id']) for r in fixf['resources']]))
m = json.load(open(os.path.join(fx, 'mtht_expected.json')))
bad = []
for inst in m['instances']:
    o = run('mtht', '1', str(inst['seed']), str(inst['question_number']))
    if not o: bad.append('run'); continue
    got = json.load(open(o))
    for k in ('answer', 'question', 'hint', 'buffer_hex'):
        if got[k] != inst[k]: bad.append((inst['seed'], inst['question_number'], k))
check('mtht instances (%d)' % len(m['instances']), not bad, str(bad[:6]))
o = os.path.join(tmp, 's.json')
r = subprocess.run([eng, '--conform', 'save-dump', os.path.join(fx, 'save_SSOWINCD.DAT'), '--out', o], capture_output=True, text=True)
def sub(e, g, path=''):
    bad = []
    if isinstance(e, dict):
        for k, v in e.items():
            if k not in g: bad.append(path + '/' + k + ' missing')
            else: bad += sub(v, g[k], path + '/' + k)
    elif isinstance(e, list):
        if len(e) != len(g): bad.append(path + ' len')
        else:
            for i, (a, b) in enumerate(zip(e, g)): bad += sub(a, b, path + '[%d]' % i)
    elif e != g: bad.append('%s: %r != %r' % (path, e, g))
    return bad
se = json.load(open(os.path.join(fx, 'save_expected.json')))
bad = sub(se, json.load(open(o))) if r.returncode == 0 else ['run failed']
check('save-dump', not bad, str(bad[:5]))
rl = json.load(open(os.path.join(fx, 'rules.json')))
bad = []
for st in rl['drill_sets']['sets']:
    o = run('drill', str(st['tier']), str(st['srand']), str(len(st['questions'])))
    got = json.load(open(o))
    if got['customisation'] != st['customisation']: bad.append(('cust', st['tier']))
    if got['deck_size'] != rl['default_deck_sizes'][str(st['tier'])]: bad.append(('deck', st['tier'], got['deck_size']))
    if got['questions'] != st['questions']: bad.append(('questions', st['tier']))
check('drill sets', not bad, str(bad))
bad = []
for st in rl['puzzles']['sets']:
    o = run('puzzle', str(st['tier']), str(st['srand']), str(len(st['games'])))
    got = json.load(open(o))
    if got['games'] != st['games']: bad.append(('puzzle', st['tier'], got['games']))
check('puzzles', not bad, str(bad)[:600])

# ---------------------------------------------------------------- DOS audio (spec 12)
dx = os.path.join(fx, 'dos')
def dosrun(*a, pre=()):
    out = os.path.join(tmp, 'd.out')
    r = subprocess.run([eng, *pre, '--conform', *a, '--out', out], capture_output=True, text=True)
    return (out if r.returncode == 0 else None), r
ce = json.load(open(os.path.join(dx, 'check_expected.json')))
bad = []
for ep in ce['exepack']:
    o, r = dosrun('dos-unpack', os.path.join(dx, ep['file']))
    if 'expected_error' in ep:
        if o or r.returncode != 2 or ep['expected_error'] not in r.stderr: bad.append((ep['file'], 'should fail'))
    elif not o or open(o, 'rb').read() != open(os.path.join(dx, ep['expected']), 'rb').read(): bad.append(ep['file'])
check('dos-unpack', not bad, str(bad))
bad = []
for cs in ce['cases']:
    o, r = dosrun('dos-check', os.path.join(dx, cs['dir']))
    got = json.load(open(o)) if o else {}
    for k, v in cs.items():
        if k in ('dir', 'reason'): continue
        if got.get(k) != v: bad.append((cs['dir'], k))
check('dos-check', not bad, str(bad))
bad = []
for t in json.load(open(os.path.join(dx, 'trace_expected.json')))['traces']:
    o, r = dosrun('dos-trace', str(t['song']), '--variant', t['variant'], '--ticks', str(t['ticks']), pre=('--dos-game', os.path.join(dx, 'game')))
    if not o or json.load(open(o)) != t: bad.append((t['variant'], t['song'], t['ticks']))
check('dos-trace', not bad, str(bad))
bad = []
for k, v in json.load(open(os.path.join(dx, 'script_expected.json')))['results'].items():
    var, name = k.split('/')
    o, r = dosrun('dos-script', os.path.join(dx, 'script_%s.txt' % name), '--variant', var, pre=('--dos-game', os.path.join(dx, 'game')))
    if not o or json.load(open(o)) != v: bad.append(k)
check('dos-script', not bad, str(bad))
def wav_read(p):
    import wave
    w = wave.open(p); n = w.getnframes(); d = w.readframes(n)
    return [x for x, in struct.iter_unpack('<h', d)], w.getframerate()
def renv(d, sr):
    k = int(sr * 0.01); out = []
    import math
    for i in range(len(d) // k):
        m = sum(x * x for x in d[i*k:(i+1)*k]) / k / 32768.0 ** 2
        out.append(10 * math.log10(m + 1e-12))
    return out
bad = []
for var in ('tandy', 'speaker'):
    o, r = dosrun('dos-render', '0', '--variant', var, '--rate', '44100', pre=('--dos-game', os.path.join(dx, 'game')))
    if not o: bad.append((var, 'failed')); continue
    a, sr = wav_read(o); b, _ = wav_read(os.path.join(dx, 'render_%s_song0.wav' % var))
    if abs(len(a) - len(b)) > 1: bad.append((var, 'length', len(a), len(b))); continue
    ea, eb = renv(a, sr), renv(b, sr)
    for i, (x, y) in enumerate(zip(ea, eb)):
        if (y > -40 and abs(x - y) > 1.5) or (y < -60 and x >= -60): bad.append((var, 'env', i, round(x, 1), round(y, 1))); break
check('dos-render', not bad, str(bad))

# ---- DOS audio in play mode (needs the mock data dir, not original files)
sys.path.insert(0, here)
import mkdata
mock = os.path.join(tmp, 'mock'); mkdata.main(mock)
def playrun(name, script):
    sp = os.path.join(tmp, name + '.txt'); open(sp, 'w').write(script)
    od = os.path.join(tmp, name + '.out')
    r = subprocess.run([eng, '--data', mock, '--conform', 'play', sp, '--out', od], capture_output=True, text=True, timeout=120)
    return od, r
od, r = playrun('dosplay', 'seed time 1\naudio tandy tandy %s\nsignin TEST\nwait 300\nstate s1\n' % os.path.join(dx, 'game'))
ev = open(os.path.join(od, 'events.log')).read().splitlines() if os.path.exists(os.path.join(od, 'events.log')) else []
st = json.load(open(os.path.join(od, 's1.json'))) if os.path.exists(os.path.join(od, 's1.json')) else {}
ini = open(os.path.join(od, 'save', 'outnumbered.ini')).read() if os.path.exists(os.path.join(od, 'save', 'outnumbered.ini')) else ''
ok = (any(l.endswith('DOS PLAY 1 accepted') for l in ev) and st.get('audio', {}).get('dos_status') == 'ok' and st['audio']['music_style'] == 'tandy'
      and st['audio']['shared_voice'] is True and 'MusicStyle=tandy' in ini)
ts = [int(l.split()[0]) for l in ev]
check('dos play mode', ok and ts == sorted(ts), str(st.get('audio')))
od, r = playrun('dosmiss', 'seed time 1\naudio speaker tandy /nonexistent/dir\nsignin TEST\nwait 100\nstate s1\n')
st = json.load(open(os.path.join(od, 's1.json'))) if os.path.exists(os.path.join(od, 's1.json')) else {}
a = st.get('audio', {})
check('dos fallback', a.get('dos_status') == 'missing' and a.get('music_style') == 'windows' and a.get('sfx_style') == 'windows' and 'DOS audio unavailable' in r.stderr, str(a) + r.stderr)
od, r = playrun('dossep', 'seed time 1\naudio tandy speaker %s\nsignin TEST\nwait 300\nstate s1\n' % os.path.join(dx, 'game'))
ev = open(os.path.join(od, 'events.log')).read().splitlines() if os.path.exists(os.path.join(od, 'events.log')) else []
st = json.load(open(os.path.join(od, 's1.json'))) if os.path.exists(os.path.join(od, 's1.json')) else {}
check('dos separate drivers', st.get('audio', {}).get('shared_voice') is False and any(l.endswith('DOS PLAY 1 accepted music') for l in ev), str(st.get('audio')))

# ---------------------------------------------------------------- input containers (spec 13)
cx = os.path.join(fx, 'containers')
bad = []
for cs in json.load(open(os.path.join(cx, 'expected.json')))['cases']:
    out = os.path.join(tmp, 'lf.json')
    if os.path.exists(out): os.remove(out)
    r = subprocess.run([eng, '--conform', 'list-files', os.path.join(cx, cs['path']), '--role', cs['role'], '--out', out], capture_output=True, text=True)
    got = json.load(open(out)) if os.path.exists(out) else {}
    e = cs['expected']
    if e['ok']:
        if got != e or r.returncode != 0: bad.append(cs['case'])
    elif got.get('ok') is not False or got.get('error') != e['error'] or r.returncode != 2: bad.append(cs['case'])
check('list-files (%d cases)' % len(json.load(open(os.path.join(cx, 'expected.json')))['cases']), not bad, str(bad))
o, r = dosrun('dos-check', os.path.join(cx, 'dos_nested.zip'))
got = json.load(open(o)) if o else {}
check('dos-check zip', got.get('ok') is True and got.get('unpacked_sha256') == ce['cases'][0]['unpacked_sha256'], str(got)[:200])
o, r = dosrun('dos-trace', '0', '--variant', 'tandy', '--ticks', '4000', pre=('--dos-game', os.path.join(cx, 'dos_deflate.zip')))
tr = [t for t in json.load(open(os.path.join(dx, 'trace_expected.json')))['traces'] if t['variant'] == 'tandy' and t['song'] == 0 and t['ticks'] == 4000][0]
check('dos-trace from zip', bool(o) and json.load(open(o)) == tr)
import shutil
if shutil.which('xorriso'):
    iso_root = os.path.join(tmp, 'isoroot', 'SSOWINCD'); shutil.copytree(mock, iso_root)
    iso = os.path.join(tmp, 'mock.iso')
    xr = subprocess.run(['xorriso', '-as', 'mkisofs', '-iso-level', '3', '-o', iso, os.path.join(tmp, 'isoroot')], capture_output=True, text=True)
    if xr.returncode or not os.path.exists(iso): print('  xorriso failed', xr.returncode, xr.stdout.strip()[-400:], xr.stderr.strip()[-300:], os.listdir(tmp))
    sp = os.path.join(tmp, 'cont.txt'); open(sp, 'w').write('seed time 1\nsignin TEST\npress ENTER\nwait 200\nstate s9\n')
    res = []
    for n, src in (('dir', mock), ('iso', iso)):
        od = os.path.join(tmp, 'cont_' + n)
        rr = subprocess.run([eng, '--data', src, '--conform', 'play', sp, '--out', od], capture_output=True, text=True, timeout=120)
        if rr.returncode: print('  play failed', n, rr.stderr.strip()[:300])
        res.append(tuple(open(os.path.join(od, f)).read() if os.path.exists(os.path.join(od, f)) else None for f in ('s9.json', 'events.log')))
    import difflib
    dd = '' if res[0] == res[1] else str([l for l in difflib.unified_diff((res[0][0] or '').splitlines(), (res[1][0] or '').splitlines(), lineterm='')][:8]) + str([l for l in difflib.unified_diff((res[0][1] or '').splitlines(), (res[1][1] or '').splitlines(), lineterm='')][:8])
    check('play from ISO equals folder', res[0][0] is not None and res[0] == res[1], dd)
# template database (13 section 6.1): players of the data source's SSOWINCD.DAT are found with an empty save directory
m2 = os.path.join(tmp, 'mock2'); shutil.copytree(mock, m2)
shutil.copy(os.path.join(cx, 'folder_data', 'SSOWINCD', 'SSOWINCD.DAT'), m2)
sp = os.path.join(tmp, 'tmpl.txt'); open(sp, 'w').write('seed time 1\nsignin ALICE\nwait 100\nstate s1\n')
od = os.path.join(tmp, 'tmpl.out')
subprocess.run([eng, '--data', m2, '--conform', 'play', sp, '--out', od], capture_output=True, text=True, timeout=120)
evs = open(os.path.join(od, 'events.log')).read() if os.path.exists(os.path.join(od, 'events.log')) else ''
check('template database', 'DIALOG 145' in evs and os.path.getsize(os.path.join(od, 'save', 'SSOWINCD.DAT')) == 18140, evs)

# ---------------------------------------------------------------- first-run setup (spec 14)
sx = os.path.join(fx, 'setup')
sexp = json.load(open(os.path.join(sx, 'expected.json')))
def cmp_install(got, e):
    bad = []
    for k in ('ok', 'error', 'messages', 'installed', 'tree'):
        if got.get(k) != e[k]: bad.append(k)
    for r in ('windows', 'dos'):
        for k in ('state', 'error', 'source_kind', 'folder', 'files', 'bytes'):
            if got['rows'][r].get(k) != e['rows'][r].get(k): bad.append('rows.%s.%s' % (r, k))
    gm, em = got.get('manifest'), e.get('manifest')
    if (gm is None) != (em is None): bad.append('manifest')
    elif em:
        if gm.get('format') != em.get('format'): bad.append('format')
        for p in ('game', 'dos'):
            a, b = gm.get(p), em.get(p)
            if (a is None) != (b is None): bad.append(p + ' null'); continue
            if b:
                for k in ('source_kind', 'folder'):
                    if a.get(k) != b.get(k): bad.append('%s.%s' % (p, k))
                if a['files'] != b['files']: bad.append(p + '.files')
    return bad
bad = []
for c in sexp['install']:
    t = tempfile.mkdtemp()
    home = c['home'].replace('<T>', t)
    if c['start_home']: shutil.copytree(os.path.join(sx, 'homes', c['start_home']), home)
    for st in c['steps']:
        out = os.path.join(t, 'r.json')
        subprocess.run([eng, '--conform', 'install', '--home', home, *[os.path.join(fx, x) for x in st['sources']], '--out', out], capture_output=True, text=True)
        got = json.load(open(out)) if os.path.exists(out) else None
        b = cmp_install(got, st['expected']) if got else ['no output']
        if b: bad.append((c['case'], b[:3])); break
    shutil.rmtree(t, ignore_errors=True)
check('setup install (%d cases)' % len(sexp['install']), not bad, str(bad))
bad = []
for c in sexp['resolve']:
    if os.name == 'nt' and c['case'].startswith('xdg'): continue          # XDG_DATA_HOME is a POSIX rule; Windows uses %LOCALAPPDATA% (14 section 1.1)
    t = tempfile.mkdtemp()
    home = c['home'].replace('<T>', t)
    if c['home_fixture']: shutil.copytree(os.path.join(sx, 'homes', c['home_fixture']), home)
    sub = lambda v: v.replace('<home>', home).replace('<T>', t)
    env = dict(os.environ)
    for k in ('XDG_DATA_HOME', 'OUTNUMBERED_HOME'): env.pop(k, None)
    env.update({k: sub(v) for k, v in c['env'].items()})
    out = os.path.join(t, 'r.json')
    subprocess.run([eng, '--conform', 'resolve', *[sub(a) for a in c['args']], '--out', out], capture_output=True, text=True, env=env)
    got = json.load(open(out)) if os.path.exists(out) else None
    if got != {k: (sub(v) if isinstance(v, str) else v) for k, v in c['expected'].items()}: bad.append(c['case'])
    shutil.rmtree(t, ignore_errors=True)
check('setup resolve (%d cases)' % len(sexp['resolve']), not bad, str(bad))
if os.name == 'nt':                                                       # the default base folder on Windows (14 section 1.1)
    t = tempfile.mkdtemp()
    env = dict(os.environ)
    for k in ('XDG_DATA_HOME', 'OUTNUMBERED_HOME'): env.pop(k, None)
    env['LOCALAPPDATA'] = os.path.join(t, 'Ünï Cödé', 'Local')
    out = os.path.join(t, 'r.json')
    subprocess.run([eng, '--conform', 'resolve', '--out', out], capture_output=True, text=True, env=env)
    got = json.load(open(out, encoding='utf-8')) if os.path.exists(out) else {}
    want = (env['LOCALAPPDATA'] + '/OutNumbered').replace('\\', '/')
    check('Windows default base folder: %LOCALAPPDATA%/OutNumbered (non-ASCII path)', str(got.get('home', '')).replace('\\', '/') == want, str(got) + ' want ' + want)
    shutil.rmtree(t, ignore_errors=True)
def strip_h(o):
    if isinstance(o, dict): return {k: strip_h(v) for k, v in o.items() if k not in ('home', 'name', 'text')}
    if isinstance(o, list): return [strip_h(x) for x in o]
    return o
t = tempfile.mkdtemp()
subprocess.run([eng, '--home', os.path.join(t, 'home'), '--conform', 'play', os.path.join(sx, 'play', 'setup_basic.txt'), '--out', os.path.join(t, 'out')], capture_output=True, text=True, timeout=120)
bad = []
for name, e in sexp['play']['states'].items():
    p = os.path.join(t, 'out', name + '.json')
    if not os.path.exists(p): bad.append(name + ' missing'); continue
    g = json.load(open(p))
    gs, es = strip_h(g.get('setup', {})), strip_h(e)
    d = [k for k in es if gs.get(k) != es[k]]
    if d or g.get('screen') != 'setup': bad.append((name, d))
check('setup play script (%d states)' % len(sexp['play']['states']), not bad, str(bad))
shutil.rmtree(t, ignore_errors=True)

# a complete game folder: install it on the setup screen, the game starts in the same process and the script goes on
t = tempfile.mkdtemp()
sp = os.path.join(t, 's.txt')
open(sp, 'w').write('setup\ndrop %s\npress TAB\npress ENTER\nwait 1500\nstate a\nwait 100\nstate b\n' % mock)
subprocess.run([eng, '--home', os.path.join(t, 'home'), '--conform', 'play', sp, '--out', os.path.join(t, 'out')], capture_output=True, text=True, timeout=120)
sb = os.path.join(t, 'out', 'b.json')
gb = json.load(open(sb)) if os.path.exists(sb) else {}
evs = open(os.path.join(t, 'out', 'events.log')).read() if os.path.exists(os.path.join(t, 'out', 'events.log')) else ''
check('setup install, then the game starts', gb.get('screen') not in (None, 'setup') and 'MUSIC 1724' in evs, str(gb.get('screen')))
shutil.rmtree(t, ignore_errors=True)

# ---------------------------------------------------------------- audio levels (spec 15)
lx = os.path.join(fx, 'audio_levels')
lexp = json.load(open(os.path.join(lx, 'expected.json')))
bad = []
for on in (1, 0):
    out = os.path.join(tmp, 'al.json')
    args = [eng, '--conform', 'audio-levels', '--even-out', str(on)]
    for c in lexp['cases']: args += ['--wav', os.path.join(lx, c['file']), c['bus']]
    subprocess.run(args + ['--out', out], capture_output=True, text=True)
    g = json.load(open(out)) if os.path.exists(out) else {'assets': []}
    if len(g['assets']) != len(lexp['cases']): bad.append(('count', on)); continue
    for c, a_ in zip(lexp['cases'], g['assets']):
        near = lambda x, y, tol: (x is None and y is None) or (x is not None and y is not None and abs(x - y) <= tol)
        if not (near(a_['lufs'], c['lufs'], 0.05) and near(a_['peak_dbfs'], c['peak_dbfs'], 0.05) and near(a_['gain_db'], c['gain_db_even_on' if on else 'gain_db_even_off'], 0.1)):
            bad.append((c['file'], on))
check('audio-levels --wav (%d files, even out on and off)' % len(lexp['cases']), not bad, str(bad))
bad = []
for sl in lexp['slider_db']:
    out = os.path.join(tmp, 'al.json')
    subprocess.run([eng, '--conform', 'audio-levels', '--volumes', '%d,100,100' % sl['value'], '--wav', os.path.join(lx, lexp['cases'][0]['file']), 'music', '--out', out], capture_output=True, text=True)
    g = json.load(open(out))['slider_db']['music']
    if not ((g is None and sl['db'] is None) or (g is not None and sl['db'] is not None and abs(g - sl['db']) <= 0.01)): bad.append((sl['value'], g))
check('slider law', not bad, str(bad))
t = tempfile.mkdtemp()
sp = os.path.join(t, 's.txt')
open(sp, 'w').write('seed time 1\nvolume 80 50 0 0\nsignin TEST\nwait 50\nstate s1\n')
subprocess.run([eng, '--data', mock, '--conform', 'play', sp, '--out', os.path.join(t, 'o')], capture_output=True, text=True, timeout=120)
g1 = json.load(open(os.path.join(t, 'o', 's1.json'))) if os.path.exists(os.path.join(t, 'o', 's1.json')) else {}
ini = open(os.path.join(t, 'o', 'save', 'outnumbered.ini')).read() if os.path.exists(os.path.join(t, 'o', 'save', 'outnumbered.ini')) else ''
au = g1.get('audio', {})
check('volume script command, state and INI', au.get('volumes') == {'music': 80, 'effects': 50, 'speech': 0} and au.get('even_out') is False and 'MusicVolume=80' in ini and 'EvenOutVolume=0' in ini, str(au))
shutil.rmtree(t, ignore_errors=True)

# ---------------------------------------------------------------- menu bar (spec 03 section 3.0)
t = tempfile.mkdtemp()
sp = os.path.join(t, 's.txt')
open(sp, 'w').write('intro\nwait 10\npress SPACE\nwait 20\nframe c0\nframe f0 full\nmenuclick 60 10\nwait 3\nframe f1 full\nmenuclick 60 67\nwait 3\nstate s1\n')
subprocess.run([eng, '--data', mock, '--conform', 'play', sp, '--out', os.path.join(t, 'o')], capture_output=True, text=True, timeout=120)
def png_size(p):
    d = open(p, 'rb').read()
    return struct.unpack('>II', d[16:24])
try:
    sizes = (png_size(os.path.join(t, 'o', 'c0.png')), png_size(os.path.join(t, 'o', 'f0.png')))
except Exception:
    sizes = None
evs = open(os.path.join(t, 'o', 'events.log')).read() if os.path.exists(os.path.join(t, 'o', 'events.log')) else ''
check('menu bar: frame sizes, menuclick opens a title and runs an item', sizes == ((512, 384), (512, 404)) and 'DIALOG 22002' in evs, str(sizes))
shutil.rmtree(t, ignore_errors=True)

# ---------------------------------------------------------------- the bar on every screen (M1)
def bar_run(script):
    t = tempfile.mkdtemp()
    sp = os.path.join(t, 's.txt')
    open(sp, 'w').write(script)
    subprocess.run([eng, '--data', mock, '--conform', 'play', sp, '--out', os.path.join(t, 'o')], capture_output=True, text=True, timeout=120)
    ev = open(os.path.join(t, 'o', 'events.log')).read() if os.path.exists(os.path.join(t, 'o', 'events.log')) else ''
    st = {}
    for n in os.listdir(os.path.join(t, 'o')) if os.path.isdir(os.path.join(t, 'o')) else []:
        if n.endswith('.json'): st[n[:-5]] = json.load(open(os.path.join(t, 'o', n)))
    shutil.rmtree(t, ignore_errors=True)
    return ev, st
ev, st = bar_run('intro\nwait 10\nmenuclick 60 10\nwait 3\nmenuclick 60 67\nwait 3\nstate s1\nmenu 603\nwait 3\nstate s2\n')
check('bar on the title screen: titles open, every item greyed (no command runs), the title is not skipped', ev.count('DIALOG 22002') == 0 and st.get('s1', {}).get('screen') == 'title' and st.get('s2', {}).get('screen') == 'title', ev)
ev, st = bar_run('seed time 1\nsignin TEST\nwait 50\npress TAB\nwait 20\nstate d0\nmenu 1103\nwait 5\nmenuclick 60 10\nwait 3\nmenuclick 60 67\nwait 3\nstate d1\n')
check('bar on the decoder: menu command and clicks, decoder stays open, clock stays paused',
      ev.count('DIALOG 22002') == 2 and st.get('d1', {}).get('screen') == 'decoder' and st.get('d1', {}).get('clock_paused') == 1, ev[-200:])
ev, st = bar_run('drill\nwait 600\nmenuclick 60 10\nwait 3\nmenuclick 60 49\nwait 3\nstate r1\nmenu 1401\nwait 3\nstate r2\n')
check('bar on the drill question: clicks and menu command, drill stays', ev.count('DIALOG 22002') == 2 and st.get('r2', {}).get('screen') == 'drill', ev[-200:])

# ---------------------------------------------------------------- v1.31: an invalid saved game is discarded at sign-in
import struct as _st
d1 = tempfile.mkdtemp()
sp1 = os.path.join(d1, 's.txt')
open(sp1, 'w').write('seed time 1\nsignin TEST\nwait 20\nstate a\n')
subprocess.run([eng, '--data', mock, '--conform', 'play', sp1, '--out', os.path.join(d1, 'o1')], capture_output=True, text=True, timeout=120)
dbp = os.path.join(d1, 'o1', 'save', 'SSOWINCD.DAT')
ok = False
try:
    b = bytearray(open(dbp, 'rb').read())
    slot = _st.unpack('<H', b[8 + 13:8 + 15])[0]
    off = 1508 + slot * 168
    b[off + 0x2B] = 1
    b[off + 0x2C:off + 0x2C + 99] = bytes(99)               # saved flag set, all-zero state (the P14 record)
    os.makedirs(os.path.join(d1, 'o2', 'save'))
    open(os.path.join(d1, 'o2', 'save', 'SSOWINCD.DAT'), 'wb').write(b)
    subprocess.run([eng, '--data', mock, '--conform', 'play', sp1, '--out', os.path.join(d1, 'o2')], capture_output=True, text=True, timeout=120)
    g = json.load(open(os.path.join(d1, 'o2', 'a.json')))
    b2 = open(os.path.join(d1, 'o2', 'save', 'SSOWINCD.DAT'), 'rb').read()
    ok = g.get('clock') == 900 and b2[off + 0x2B] == 0
except Exception as e_:
    ok = False
check('invalid saved state (all zero) is discarded: new game, saved flag cleared', ok)
shutil.rmtree(d1, ignore_errors=True)

# ---------------------------------------------------------------- close box: Exit confirmation, then save only a live game
d3 = tempfile.mkdtemp()
open(os.path.join(d3, 's.txt'), 'w').write('seed time 1\nsignin TEST\nwait 20\nclose\nwait 20\nstate a\n')
subprocess.run([eng, '--data', mock, '--conform', 'play', os.path.join(d3, 's.txt'), '--out', os.path.join(d3, 'o')], capture_output=True, text=True, timeout=120)
try:
    b = open(os.path.join(d3, 'o', 'save', 'SSOWINCD.DAT'), 'rb').read()
    ok = (not os.path.exists(os.path.join(d3, 'o', 'a.json'))) and b[1508 + 0x2B] == 1 and _st.unpack('<H', b[1508 + 0x2C + 4:1508 + 0x2C + 6])[0] == 900
except Exception:
    ok = False
check('close box in a live game: the program ends (no dialog resource) and saves the initialised state', ok)
shutil.rmtree(d3, ignore_errors=True)

# ---------------------------------------------------------------- DOS elevator ride (spec 12 section 5.6a)
import re as _re
t = tempfile.mkdtemp()
sp = os.path.join(t, 's.txt')
open(sp, 'w').write('seed time 1\naudio tandy tandy %s\nsignin TEST\nwait 30\nkey down LEFT\nwait 620\nkey up LEFT\nwait 8\nkey down UP\nwait 5\nkey up UP\nwait 420\nkey down UP\nwait 5\nkey up UP\nwait 700\nstate z\n' % os.path.join(dx, 'game'))
subprocess.run([eng, '--data', mock, '--conform', 'play', sp, '--out', os.path.join(t, 'o')], capture_output=True, text=True, timeout=180)
evl = open(os.path.join(t, 'o', 'events.log')).read().splitlines() if os.path.exists(os.path.join(t, 'o', 'events.log')) else []
def ticks(pat): return [int(l.split()[0]) for l in evl if _re.search(pat, l)]
opens, closes, p22, p21, stops = ticks(r'SFX 1713'), ticks(r'SFX 1714'), ticks(r'DOS PLAY 22 '), ticks(r'DOS PLAY 21 '), ticks(r'DOS STOP')
ok = len(opens) >= 4 and len(closes) >= 4 and len(p22) == 1 and len(p21) == 1
if ok:
    ok = closes[0] < p22[0] < opens[1] and closes[2] < p21[0] < opens[3]                       # once per ride, after the departure close
    ok = ok and not any(closes[0] <= x <= closes[1] or closes[2] <= x <= closes[3] for x in stops)   # the arrival's stop-all and close do not cut it
check('DOS elevator ride: ride song once per ride (22 up, 21 down) after the departure close; arrival close silent, nothing stops it', ok, str((opens, closes, p22, p21, stops)))
shutil.rmtree(t, ignore_errors=True)

# ---------------------------------------------------------------- style options: --name=V and --name V, bad values are errors (12 section 1.2)
t = tempfile.mkdtemp()
sp = os.path.join(t, 's.txt')
open(sp, 'w').write('seed time 1\nsignin TEST\nwait 20\nstate s1\n')
def styles(*opts):
    o = os.path.join(t, 'o_' + str(abs(hash(opts))))
    r = subprocess.run([eng, '--data', mock, '--dos-game', os.path.join(dx, 'game'), *opts, '--conform', 'play', sp, '--out', o], capture_output=True, text=True, timeout=120)
    a = json.load(open(os.path.join(o, 's1.json')))['audio'] if os.path.exists(os.path.join(o, 's1.json')) else {}
    return r, a.get('music_style'), a.get('sfx_style')
r1, m1, e1 = styles('--music-style=tandy', '--sfx-style=speaker')
r2, m2, e2 = styles('--music-style', 'tandy', '--sfx-style', 'speaker')
r3, m3, e3 = styles('--dos-audio', 'speaker')
r4, m4, e4 = styles('--music-style', 'bogus')
check('style options: "=" and space forms are the same; a bad value is an error', (m1, e1) == ('tandy', 'speaker') and (m2, e2) == ('tandy', 'speaker') and (m3, e3) == ('speaker', 'speaker') and r4.returncode == 1 and 'expected windows, tandy or speaker' in r4.stderr, str((m1, e1, m2, e2, m3, e3, r4.returncode, r4.stderr)))
shutil.rmtree(t, ignore_errors=True)
sys.exit(1 if fails else 0)
