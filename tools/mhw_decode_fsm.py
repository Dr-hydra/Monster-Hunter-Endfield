"""Invoke pinned Strackeror/MHW-Fsm using its supported Construct 2.10.56.
The sole adapter preserves strings with an explicit fallback instead of silently
discarding bytes through upstream UTF-8 'ignore'. Use bundled Python 3.11.
"""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'local/tools/mhw-construct-old/construct-2.10.56'))
sys.argv=['fsm_handling.py',str(ROOT/'research/mhw-original/extracted/hm/wp/wp03/wp03_action.fsm')]
source=ROOT/'local/tools/mhw-fsm/fsm_handling.py'
replacement="""try:
        return obj.decode(self.encoding, 'strict')
    except UnicodeDecodeError:
        try:
            return obj.decode('cp932', 'strict')
        except UnicodeDecodeError:
            return obj.decode(self.encoding, 'backslashreplace')"""
text=source.read_text(encoding='utf-8').replace("return obj.decode(self.encoding,'ignore')",replacement)
exec(compile(text,str(source),'exec'),dict(__name__='__main__'))
print('FSM decoded with explicit string fallback and unmodified numeric fields')
