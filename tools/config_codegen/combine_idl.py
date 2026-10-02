#
# (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
#
# RTI grants Licensee a license to use, modify, compile, and create derivative
# works of the Software. Licensee has the right to distribute object form only
# for use with RTI products. The Software is provided "as is", with no warranty
# of any type, including any warranty for fitness for any purpose. RTI is under no
# obligation to maintain or support the Software. RTI shall not be liable for any
# incidental or consequential damages arising out of the use or inability to use
# the software.
#

import sys
from pathlib import Path
output = Path(sys.argv[1])
output.write_text(output.read_text() + "".join("\n" + Path(p).read_text() for p in sys.argv[2:]))
