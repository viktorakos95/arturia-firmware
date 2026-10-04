import sys
a,b=sys.argv[1].lower(),sys.argv[2].lower()
for l in open('local/stock_full.dis'):
    ad=l[:8]
    if a<=ad<=b:
        p=l.split(None,2)
        print(ad,' '.join(p[2].split()))
