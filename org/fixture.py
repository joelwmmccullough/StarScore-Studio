import os, json, sys
R=sys.argv[1]; B=R+'/Sheets and Demos'; P=R+'/Projects and Sheets'
def pdf(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    content=f"BT /F1 12 Tf 72 720 Td ({text}) Tj ET".encode()
    objs=[b"<< /Type /Catalog /Pages 2 0 R >>", b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
          b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>",
          b"<< /Length %d >>\nstream\n" % len(content) + content + b"\nendstream", b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"]
    out=b"%PDF-1.4\n"; offs=[]
    for i,o in enumerate(objs):
        offs.append(len(out)); out+=b"%d 0 obj\n" % (i+1) + o + b"\nendobj\n"
    x=len(out); out+=b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs)+1)
    for o in offs: out+=b"%010d 00000 n \n" % o
    out+=b"trailer << /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs)+1, x)
    open(path,'wb').write(out)
songs={'1 Test Song':'TEST','2 Other Tune':'OTHR'}
for root,code in songs.items():
    pdf(f'{B}/{root}/1 Lead Sheet/{code} - Lead Sheet.pdf','Lead Sheet')
    for r in ['Bass','Drums','Guitar','Keys']: pdf(f'{B}/{root}/1 Rhythm/{code} - {r}.pdf',r)
    for h in ['Score','Trumpet','Alto Sax','Tenor Sax']: pdf(f'{B}/{root}/3H Tpt Alt Ten/{code} - {h}.pdf',h)
    os.makedirs(f'{B}/{root}/Update Notes 26-08-19',exist_ok=True)
pdf(f'{B}/6 Inbox/TEST - Trombone.pdf','Trombone')
org=B+'/6 Inbox/.organizer'; os.makedirs(org,exist_ok=True)
json.dump(songs,open(org+'/codes.json','w'))
json.dump({"players":[{"name":"Player A","instruments":["Trumpet"],"blurb":"trumpet","current":True,"horn":True,"chairs":{"3":{"chair":1,"key":"Bb","instrument":"trumpet"}}},
  {"name":"Player B","instruments":["Bass"],"blurb":"bass","current":True}]},open(org+'/roster.json','w'))
json.dump({"version":1,"shows":[{"id":"s1","band":"Starsign","date":"2026-08-15","venue":"A Venue","video":"AbCdEfGhIjK","timestamps":True,"status":"filmed"}],
  "performances":[{"id":"p0001","song":"TEST","show":"s1","seconds":125,"rating":4}],"releases":[{"id":"r001","song":"TEST","kind":"album","video":"AbCdEfGhIjK","date":"2026-07-24","label":"Album"}],
  "playlists":{"live":"https://www.youtube.com/playlist?list=x"}},open(org+'/recordings.json','w'))
os.makedirs(P+'/1 Starsign Originals/Test Song',exist_ok=True); open(P+'/1 Starsign Originals/Test Song/TEST - Test Song.starscore','w').write('x')
os.makedirs(P+'/.organizer',exist_ok=True); json.dump({"Test Song":"TEST"},open(P+'/.organizer/codes_proj.json','w'))
