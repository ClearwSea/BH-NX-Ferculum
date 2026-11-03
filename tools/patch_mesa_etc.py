"""Enable opt-in Mesa ETC software fallback in the pinned mesa32 checkout."""
import argparse
import hashlib
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("mesa", type=Path)
a = p.parse_args()
target = a.mesa / "src/gallium/drivers/nouveau/nvc0/nvc0_screen.c"
source = target.read_text()
anchor = "   /* Restrict ETC2 and ASTC formats here."
addition = '''   /* Some emulators lack GM20B ETC texture descriptors. Let the state
    * tracker use its existing ETC decoder; hardware keeps native support. */
   if (desc->layout == UTIL_FORMAT_LAYOUT_ETC &&
       debug_get_bool_option("NOUVEAU_SOFTWARE_ETC", false))
      return false;

'''
if addition in source:
    print("Mesa ETC fallback patch already applied")
else:
    digest = hashlib.sha256(source.encode()).hexdigest()
    if digest != "55ac22216ff2bcf40b0c4aa8c1848ac956e1b5adc1c5ae6d068739f7fc9bc721":
        raise SystemExit("Unsupported Mesa source; preserve existing changes")
    target.write_text(source.replace(anchor, addition + anchor, 1), newline="\n")
    print("Applied opt-in NOUVEAU_SOFTWARE_ETC; rebuild mesa and install prefix")
