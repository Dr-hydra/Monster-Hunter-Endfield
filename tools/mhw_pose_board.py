"""Fixed-scale original source skeleton comparison; no fitted sword axes."""
from pathlib import Path
import json
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
ROOT=Path(__file__).resolve().parents[1]
CHAINS=[[0,1,2,3,4],[2,5,6,7,8],[2,9,10,11,12],[0,13,14,15,16],[0,17,18,19,20]]
fig,axes=plt.subplots(4,7,figsize=(18,12),layout='constrained')
for i,slot in enumerate([170,171,172,173]):
    d=json.loads((ROOT/f'research/mhw-original/baked/preview/slot_{slot:03d}.json').read_text())
    for j in range(7):
        frame=round((len(d['positions'])-1)*j/6);p=d['positions'][frame];ax=axes[i,j]
        for chain in CHAINS:ax.plot([p[k][0] for k in chain],[p[k][1] for k in chain],color='#136a96',lw=2,marker='o',markersize=2)
        ax.scatter([p[12][0]],[p[12][1]],color='#e34421',s=30)
        ax.axhline(0,color='gray');ax.set_aspect('equal');ax.set_xlim(-1.2,1.2);ax.set_ylim(-.3,4.6)
        ax.set_title(f'{slot}: {frame}/{len(d["positions"])-1}',fontsize=10);ax.grid(alpha=.15)
fig.suptitle('Original MHW front projection | red = dominant wrist | no imposed height or blade direction',fontsize=15)
fig.savefig(ROOT/'research/mhw-original/source-pose-board.png',dpi=120)
print('source-pose-board.png generated')
