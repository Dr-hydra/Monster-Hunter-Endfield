from pathlib import Path
import json, math
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[1]
edges=[(0,1),(1,2),(2,3),(3,4),(2,5),(5,6),(6,7),(7,8),(2,9),(9,10),(10,11),(11,12),(0,13),(13,14),(14,15),(15,16),(0,17),(17,18),(18,19),(19,20)]
slots=[1,3,50,101,102,103,104,105,106,107,108,109,110,111,127,128,130,132,150,153,154,156,157,158,160,301,303,304,305,306,307,308,312,318,319,326,332,335,339,340,341,342,343,345,346,347,460,461,463]
def sheet(selected,filename,width=1800,row_height=230):
    canvas=Image.new('RGB',(width,len(selected)*row_height),(18,22,30)); d=ImageDraw.Draw(canvas)
    for row,slot in enumerate(selected):
        path=ROOT/'preview'/f'slot_{slot:03}.json'
        if not path.exists(): continue
        data=json.loads(path.read_text()); frames=data['positions']
        d.text((8,row*row_height+8),f'Slot {slot} / {len(frames)}f',fill='white')
        for sample,t in enumerate((0,.2,.4,.6,.8,1)):
            index=round(t*(len(frames)-1)); p=frames[index]; center=200+sample*(width-250)/6
            origin=frames[0][0]
            def project(v):
                x,y,z=v; return (center+(x-origin[0] + (z-origin[2])*.45)*95,row*row_height+200-y*95)
            for a,b in edges: d.line((project(p[a]),project(p[b])),fill=(103,195,240) if a<9 else (240,150,100),width=3)
            for point in p: x,y=project(point); d.ellipse((x-3,y-3,x+3,y+3),fill='white')
            # 247 is colocated with the right hand. RGB marks its XYZ basis.
            if 'attachments' in data:
                a=data['attachments'][index]['247']; start=a['position']
                for axis,color in zip(a['axes'],('#f86868','#72f077','#6daaff')):
                    end=[v+w*.7 for v,w in zip(start,axis)]
                    d.line((project(start),project(end)),fill=color,width=2)
            d.text((center-10,row*row_height+205),str(index),fill='gray')
    canvas.save(ROOT/'preview'/filename)

sheet(slots,'source_contact_sheet.png')
sheet([101,102,103,104,105,106,107,108,109,110,111],'normal_spirit_detail.png')
sheet([127,128,130,132,150,153,306,307,308,312],'sheath_iai_detail.png')
sheet(list(range(1,16)),'basic_detail.png')
print(ROOT/'preview/source_contact_sheet.png')
