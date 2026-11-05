"""
matrix.py - builds the glove firmware with every feature switched off in turn (plus
whatever depends on it), the debug combinations, the minimum set and everything off,
runs each build for 4000 loop passes with some app commands, and checks that the
dependency checks in features.h stop impossible combinations. Needs python3 and g++.
Run:  python3 tools/firmware_check/matrix.py
"""
import subprocess, sys, os, tempfile
HERE=os.path.dirname(os.path.abspath(__file__))
TMP=tempfile.mkdtemp(prefix="glove_matrix_")
F=["ACCEL","STEPS","STILL","HAND_POSE","IR","TRACKER","STEP_CAL","IR_PLOT","WIFI","READ_FAST","READ_SLOW","READ_CACHE","ROAM","LINK_UDP","LINK_SERIAL","TELEMETRY","COMMANDS","SCREEN","BUTTONS"]
D=["DEBUG_SCREEN","DEBUG_TO_APP"]
def ok(c):  # mirror features.h dependency rules
    L=c["LINK_UDP"] or c["LINK_SERIAL"]; R=c["READ_FAST"] or c["READ_SLOW"]
    rules=[(c["STEPS"],c["ACCEL"]),(c["STILL"],c["ACCEL"]),(c["HAND_POSE"],c["ACCEL"]),(c["TRACKER"],c["STEPS"]),
      (c["STEP_CAL"],c["TRACKER"]),(c["IR_PLOT"],c["IR"] and c["HAND_POSE"] and c["TRACKER"] and L),
      (c["READ_FAST"],c["WIFI"] and L),(c["READ_SLOW"],c["WIFI"] and c["STILL"] and L),(c["READ_CACHE"],R),
      (c["ROAM"],c["WIFI"] and c["STILL"] and R),(c["LINK_UDP"],c["WIFI"]),(c["TELEMETRY"],L),(c["COMMANDS"],L),
      (c["BUTTONS"],c["SCREEN"]),(c["DEBUG_SCREEN"],c["SCREEN"]),(c["DEBUG_TO_APP"],L)]
    return [i for i,(a,b) in enumerate(rules) if a and not b]
def fix(c):  # switch off whatever needs something that is off
    while ok(c):
        for k in F+D:
            if c[k]:
                t=dict(c);
                if not ok(c): break
        bad=ok(c)
        names=["STEPS","STILL","HAND_POSE","TRACKER","STEP_CAL","IR_PLOT","READ_FAST","READ_SLOW","READ_CACHE","ROAM","LINK_UDP","TELEMETRY","COMMANDS","BUTTONS","DEBUG_SCREEN","DEBUG_TO_APP"]
        for i in bad: c[names[i]]=0
    return c
def flags(c): return ["-D%s%s=%d"%("" if k.startswith("DEBUG") else "FEAT_",k,v) for k,v in c.items()]
def build(name,c,expect_ok=True):
    r=subprocess.run([HERE+"/build.sh",TMP+"/"+name]+flags(c),capture_output=True,text=True)
    good = r.returncode==0
    if good and expect_ok:
        run=subprocess.run([TMP+"/%s/fw"%name, "C,1,HELLO\nC,2,MIRROR,ON\nC,3,DEBUG,ON\nC,4,MODE,SLOW\nC,5,CALSTEP_START\nC,6,ROAM\n"],capture_output=True,text=True,timeout=20)
        good = run.returncode==0 and "ran 4000" in run.stdout
        warn = [l for l in r.stdout.splitlines() if "warning" in l]
        print(("OK  " if good else "RUN FAIL ")+name+(" (%d warnings)"%len(warn) if warn else ""))
        if warn: print("\n".join(warn[:5]))
    elif not expect_ok:
        msg=[l for l in r.stdout.splitlines() if "#error" in l]
        print(("OK  expected error: " if not good else "FAIL (built, expected error) ")+name+"  "+(msg[0].split("#error")[-1].strip() if msg else ""))
    else:
        print("BUILD FAIL "+name); print(r.stdout[-2500:])
all_on={k:1 for k in F+D}
build("all_on",dict(all_on))
for k in F:
    c=dict(all_on); c[k]=0; build("no_"+k,fix(c))
for a,b in [(1,0),(0,1),(0,0)]:
    c=dict(all_on); c["DEBUG_SCREEN"]=a; c["DEBUG_TO_APP"]=b; build("debug_%d%d"%(a,b),c)
mini={k:0 for k in F+D}
for k in ["ACCEL","STEPS","TRACKER","LINK_SERIAL","SCREEN"]: mini[k]=1
build("minimum",mini)
build("everything_off",{k:0 for k in F+D})
c=dict(all_on); c["ACCEL"]=0; build("dep_accel_only_off",c,expect_ok=False)
c=dict(all_on); c["WIFI"]=0; build("dep_wifi_only_off",c,expect_ok=False)
