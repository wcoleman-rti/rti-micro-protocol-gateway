import sys
from pathlib import Path
output = Path(sys.argv[1])
output.write_text(output.read_text() + "".join("\n" + Path(p).read_text() for p in sys.argv[2:]))
