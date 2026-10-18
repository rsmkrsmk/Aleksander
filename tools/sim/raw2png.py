import sys,numpy as np
from PIL import Image
for fn in sys.argv[1:]:
    a=np.fromfile(fn,dtype='<u2').reshape(480,480)
    r=((a>>11)&31)*255//31; g=((a>>5)&63)*255//63; b=(a&31)*255//31
    Image.fromarray(np.dstack([r,g,b]).astype('uint8')).save(fn[:-4]+'.png')
