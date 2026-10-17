#!/usr/bin/env python3
"""Build POSI32 monochrome launcher silhouettes; no external packages required."""
from pathlib import Path
import math, struct

def artwork():
    icons=[]
    for kind in range(16):
        p=[[0]*32 for _ in range(32)]
        def disk(cx,cy,r,v=1):
            for y in range(32):
                for x in range(32):
                    if (x-cx)**2+(y-cy)**2<=r*r:p[y][x]=v
        def rect(l,t,r,b,v=1):
            for y in range(max(0,t),min(32,b+1)):
                for x in range(max(0,l),min(32,r+1)):p[y][x]=v
        def line(x1,y1,x2,y2,w=2,v=1):
            n=int(max(abs(x2-x1),abs(y2-y1))*4)+1
            for i in range(n+1):disk(x1+(x2-x1)*i/n,y1+(y2-y1)*i/n,w/2,v)
        if kind==0: # counter: tactile button and plinth
            disk(16,16,11);rect(14,10,18,22,0);rect(10,14,22,18,0)
        elif kind==1: # gear, clear hub
            disk(15.5,15.5,10)
            for n in range(8):
                a=n*math.pi/4;line(16+9*math.cos(a),16+9*math.sin(a),16+12*math.cos(a),16+12*math.sin(a),4)
            disk(15.5,15.5,4,0)
        elif kind==2: # instrument display and pulse
            rect(4,6,27,25);rect(7,9,24,22,0)
            for a,b in zip([(6,17),(10,17),(13,11),(17,22),(20,15)],[(10,17),(13,11),(17,22),(20,15),(25,15)]):line(*a,*b,1)
        elif kind==3: # folder with tab and folded front
            rect(4,7,13,11);rect(4,10,27,25);line(7,14,24,14,1,0);line(7,15,5,23,1,0)
        elif kind==4: # recovery shield with cross
            for y in range(5,27):
                half=11 if y<17 else max(1,11-(y-17))
                rect(16-half,y,16+half,y)
            rect(14,10,18,21,0);rect(10,14,22,17,0)
        elif kind==5:
            disk(16,16,12);disk(16,10,1.4,0);rect(15,15,17,23,0)
        elif kind==6: # wallpaper frame with fine parallel bands
            rect(4,5,27,26)
            for y in (11,17,23):rect(7,y,24,y,0)
        elif kind==7: # document, dog ear and lines
            rect(7,4,23,27);rect(20,4,24,9,0);line(19,5,24,10,1)
            for y in (13,17,21):rect(11,y,20,y,0)
        elif kind==8: # Wi-Fi arcs, each a connected silhouette
            for r in (7,13):
                for y in range(4,22):
                    for x in range(3,29):
                        d=math.hypot(x-15.5,y-25)
                        if abs(d-r)<1.3 and y<22:p[y][x]=1
            disk(15.5,25,2)
        elif kind==9: # double music note
            line(13,8,13,24,2);line(25,5,25,21,2);line(13,8,25,5,3)
            disk(9.5,24,3.7);disk(21.5,21,3.7)
        elif kind==10: # sharing nodes
            for a,b in [((8,16),(24,7)),((8,16),(24,25))]:line(*a,*b,2)
            for x,y in ((7,16),(24,7),(24,25)):disk(x,y,3.4)
        elif kind==11: # location pin
            disk(16,12,8)
            for y in range(14,28):
                half=max(0,(27-y)//2);rect(16-half,y,16+half,y)
            disk(16,11,3,0)
        elif kind==12: # radio handset, antenna and controls
            rect(8,11,24,27);line(11,11,8,3,2);rect(12,15,20,20,0);disk(12,24,1,0);disk(20,24,1,0)
        elif kind==13: # range plot / antenna
            line(8,26,8,8,2);line(8,26,26,26,2)
            for a,b in zip([(10,22),(14,18),(18,20),(24,10)],[(14,18),(18,20),(24,10),(27,13)]):line(*a,*b,2)
            disk(8,7,2)
        elif kind==14: # microphone capsule / cradle
            rect(12,7,20,17);disk(16,7,4);disk(16,17,4)
            line(8,15,8,19,2);line(24,15,24,19,2)
            for n in range(25):
                a=n*math.pi/24;disk(16+8*math.cos(a),19+6*math.sin(a),1)
            line(16,25,16,29,2);line(11,29,21,29,2)
        else: # folded map, with a route and location marker
            line(4,8,11,5,2);line(11,5,21,9,2);line(21,9,28,6,2)
            line(4,8,4,26,2);line(28,6,28,24,2)
            line(4,26,11,23,2);line(11,23,21,27,2);line(21,27,28,24,2)
            line(11,5,11,23,1);line(21,9,21,27,1)
            line(7,20,16,14,2);line(16,14,24,18,2);disk(24,18,2.3)
        icons.append(b''.join(struct.pack('<I',sum(v<<x for x,v in enumerate(row))) for row in p))
    return b'POI1'+b''.join(icons)
if __name__=='__main__':
    path=Path(__file__).resolve().parents[1]/'sd-payload/System/Assets/apps.icons'
    path.write_bytes(artwork());print(f'{path}: {path.stat().st_size} bytes')
