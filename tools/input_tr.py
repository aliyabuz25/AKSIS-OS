"""Send characters as physical QEMU keys for the guest's Turkish Q layout."""
def type_turkish(text,tap,key):
    plain={' ':'spc','\n':'ret','\b':'backspace','\t':'tab','i':'apostrophe','ı':'i',
           'ğ':'bracket_left','ü':'bracket_right','ş':'semicolon','ö':'comma','ç':'dot',
           '.':'slash',',':'backslash','-':'equal','*':'minus','"':'grave','<':'less'}
    shifted={'!':'1',"'":'2','^':'3','+':'4','%':'5','&':'6','/':'7','(':'8',')':'9','=':'0',
             '?':'minus','_':'equal',':':'slash',';':'backslash','>':'less',
             'I':'i','İ':'apostrophe','Ğ':'bracket_left','Ü':'bracket_right','Ş':'semicolon','Ö':'comma','Ç':'dot'}
    altgr={'@':'q','#':'3','$':'4','{':'7','[':'8',']':'9','}':'0','\\':'minus','|':'equal','€':'e'}
    for c in text:
        if c in shifted:
            key('shift',True); tap(shifted[c]); key('shift',False)
        elif c in altgr:
            key('alt_r',True); tap(altgr[c]); key('alt_r',False)
        elif c.isascii() and c.isupper():
            key('shift',True); tap(c.lower()); key('shift',False)
        else: tap(plain.get(c,c))
