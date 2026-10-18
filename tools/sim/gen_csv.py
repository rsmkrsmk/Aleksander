import datetime as D, random
random.seed(7)
now=D.datetime.now()
rows=[]
birth=D.date(2026,8,8)
for back in range(40,-1,-1):
    day=(now-D.timedelta(days=back)).date()
    t=D.datetime.combine(day,D.time(5,30))
    # karmienia co ~3h; wieczorem ostatnie ok 19:30, potem przerwa nocna
    times=[]
    cur=D.datetime.combine(day,D.time(random.choice([5,6]),random.randint(0,50)))
    while cur.hour<20 or (cur.hour==20 and False):
        times.append(cur); cur+=D.timedelta(minutes=random.randint(150,200))
    times.append(D.datetime.combine(day,D.time(random.choice([21,22]) if False else 19,random.randint(0,50))))
    if back%2: times.append(D.datetime.combine(day,D.time(22,random.randint(0,50))))
    for x in sorted(set(times)):
        if x>now: continue
        rows.append((x,'KARMIENIE',0,f"{random.randint(5,12)},{random.randint(5,12)}"))
        if random.random()<.5:
            kind=random.choice(['MLEKO_MATKI','MLEKO_MODYFIKOWANE','MLEKO_MIESZANE']); rows.append((x+D.timedelta(minutes=12),kind,random.choice([60,90,120]),''))
    # nocne karmienie po polnocy
    n=D.datetime.combine(day+D.timedelta(days=1),D.time(random.randint(2,5),random.randint(0,59)))
    if n<=now: rows.append((n,'KARMIENIE',0,"8,8"))
    if back%3==0: rows.append((D.datetime.combine(day,D.time(12,0)),'KAPIEL',0,''))
    if back%5==0 and back<=35: rows.append((D.datetime.combine(day,D.time(10,0)),'WAGA',3080+int(((now.date()-birth).days-back)*28+random.randint(-80,80)),''))
    for k in range(random.randint(4,7)):
        rows.append((D.datetime.combine(day,D.time(random.randint(6,21),random.randint(0,59))),random.choice(['PIELUCHA_MOKRA','PIELUCHA_MOKRA','PIELUCHA_BRUDNA']),0,''))
rows.sort(key=lambda r:r[0])
with open('/tmp/fw_data.csv','w') as f:
    f.write('data,godzina,typ,ml,piers_lewa_min,piers_prawa_min\n')
    for x,t,ml,extra in rows:
        f.write(f"{x:%Y-%m-%d},{x:%H:%M},{t},{ml}"+(f",{extra}" if extra else '')+"\n")
print(len(rows))
