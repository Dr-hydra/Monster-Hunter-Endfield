"""Human-readable original pose/weapon candidate review, kept separate from runtime."""
from pathlib import Path
import json,math
import numpy as np
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
CHAINS=[[0,1,2,3,4],[2,5,6,7,8],[2,9,10,11,12],[0,13,14,15,16],[0,17,18,19,20]]


def main():
    combo=json.loads((ROOT/'preview/slot_1000.json').read_text(encoding='utf-8'))
    stage=json.loads((ROOT/'research/mhw-original/helmbreaker-composition.json').read_text(encoding='utf-8'))
    source={s:json.loads((ROOT/f'research/mhw-original/baked/preview/slot_{s:03d}.json').read_text(encoding='utf-8')) for s in (170,171,172,173)}
    def render(f):
        im=Image.new('RGB',(1000,620),'#f4f6f8');draw=ImageDraw.Draw(im);p=np.array(combo['positions'][f]);phase=stage['stage_map'][f]
        draw.text((18,14),'Original MHW poses | local cosmetic fall | weapon mount candidate (not native verified)',fill='#203341')
        draw.text((18,38),f'Frame {f}/391 | {f/60:.2f}s | source {phase["slot"]}:{phase["frame"]}',fill='#203341')
        for side,(axis,center) in enumerate(((0,250),(2,750))):
            def point(v):return (center+int((v[axis]-p[0,axis])*68),560-int(v[1]*68))
            draw.line([(center-230,560),(center+230,560)],fill='#85939b',width=2)
            draw.text((center-220,72),'Rear projection' if side==0 else 'Side projection',fill='#263847')
            for chain in CHAINS:
                draw.line([point(p[i]) for i in chain],fill='#28759d',width=5)
            for joint in p:
                x,y=point(joint);draw.ellipse((x-3,y-3,x+3,y+3),fill='#28759d')
            for i in (8,12):
                x,y=point(p[i]);draw.ellipse((x-5,y-5,x+5,y+5),fill='#ce412c')
            raw=source[phase['slot']];sample=phase['frame']
            if raw['attachments']:
                attachment=raw['attachments'][sample]['247'];shift=p[12]-np.array(raw['positions'][sample][12]);origin=np.array(attachment['position'])+shift
                # Actual native bs_swo001 geometry after cancellation of imported
                # root basis: blade grows along source root +Z; attachment247
                # as drawn mount remains a candidate, never used to bend wrists.
                tip=origin+np.array(attachment['axes'][2])*2.1489
                draw.line([point(origin),point(tip)],fill='#b68420',width=5)
        return im
    frames=[render(f) for f in range(0,len(combo['positions']),4)]
    frames[0].save(ROOT/'research/mhw-original/source-combo-review.gif',save_all=True,append_images=frames[1:],duration=67,loop=0)
    at=[0,80,120,156,182,207,275,350];sheet=Image.new('RGB',(1600,992),'white')
    for i,f in enumerate(at):sheet.paste(render(f).resize((400,248)),((i%4)*400,(i//4)*248))
    sheet.crop((0,0,1600,496)).save(ROOT/'research/mhw-original/source-combo-contact.png')
    evidence={}
    for a,b,at in ((170,171,89),(171,172,46),(172,173,133)):
        left=source[a]['rotations_xyzw'][at];right=source[b]['rotations_xyzw'][0]
        angles=[math.degrees(2*math.acos(min(1.,abs(sum(x*y for x,y in zip(q,r)))))) for q,r in zip(left,right)]
        evidence[f'{a}:{at}->{b}:0']=dict(max_degrees=max(angles),right_wrist_degrees=angles[12],left_wrist_degrees=angles[8],angles=angles)
    (ROOT/'research/mhw-original/source-boundary-rotations.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')
    print('Source review GIF/contact sheet ready; native mount remains unverified')


if __name__=='__main__':main()
