"""The three retail releases of Meet the Robinsons for Wii.

All three are disc version 0.  They are the same build: the addresses of the
input library moved, and the pieces are carried over by masked-signature search.
"""
REGIONS = {
    'RRSE4Q': dict(label='Meet the Robinsons (USA)', short='USA', version=0),
    'RRSP4Q': dict(label='Meet the Robinsons (Europe, En/De/Es)', short='Europe', version=0),
    'RRSX4Q': dict(label='Meet the Robinsons (Europe, Fr/It)', short='Europe', version=0),
}

# retail DOL sizes, to give a clear error on someone else's modified dump
DOL_SIZES = {}
