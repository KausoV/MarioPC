"""Check that the painted sky moves with simulation time and redraws at the saved phase."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path
from PIL import Image, ImageChops

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable",type=Path)
    args=parser.parse_args()
    exe=args.executable.resolve()
    with tempfile.TemporaryDirectory() as directory:
        folder=Path(directory)
        for name,w,h in (("narrow",768,720),("wide",1512,648)):
            (folder/f"{name}.ini").write_text(f"width={w}\nheight={h}\nscaling=2\nvsync=0\nscreen_mode=0\n")
        quiet=folder/"start.txt"
        quiet.write_text("50 51 S\n",encoding="ascii")

        def capture(name,layout,frames,*options):
            prefix=folder/name
            subprocess.run([str(exe),"--smoke","--fast","--frames",str(frames),"--input-script",str(quiet),
                "--settings-file",str(folder/f"{layout}.ini"),"--game-save-file",str(folder/"unused.dat"),
                "--report",str(prefix.with_suffix(".json")),"--capture-graphics",str(prefix.with_suffix(".png")),
                *map(str,options)],cwd=exe.parent,check=True,timeout=90,capture_output=True)
            report=json.loads(prefix.with_suffix(".json").read_text(encoding="utf-8-sig"))
            with Image.open(prefix.with_suffix(".png")) as frame:
                # Above all playable objects and drifting low-level motes on 1-1.
                sky=frame.convert("RGB").crop((0,0,frame.width,270))
            return sky,report

        save=folder/"paused.dat"
        _,original=capture("start","narrow",280,"--level","1-1","--save-game",save)
        resized,r=capture("resized","wide",5,"--load-game",save,"--settings")
        reference,f=capture("reference","wide",280,"--level","1-1")
        assert r["core_ticks"]==0 and r["simulation_frame"]==original["simulation_frame"]
        assert resized.size==reference.size and ImageChops.difference(resized,reference).getbbox() is None
        later,l=capture("later","wide",395,"--level","1-1")
        assert f["world_camera"]==l["world_camera"]
        delta=ImageChops.difference(later,reference)
        # Stars and landscape are fixed. Cloud drift must alter more than a few specks.
        histogram=delta.convert("L").histogram()
        assert sum(histogram[3:])>300
        print("Paused resize restored identical wide sky. Cloud drift changed",
              sum(histogram[3:]),"sky pixels with the camera stationary.")

if __name__=="__main__":
    main()
