"""Hash each streamed terrain column once, immediately before installing it.

cache_stream() called column_prepare(), which checksummed and range-checked
the 12 KiB record, and then cache_load(), which repeated the identical check
before releasing the old owner. The stream path now only reads the record
in column_prepare_unchecked(); cache_load() keeps the single, mandatory
validation, so a bad record still never replaces resident terrain. The
distant-terrain and save paths keep the checked column_prepare().
"""
from build_imported import replace_once


def adapt(name, data):
    if name not in ('column_store.inc', 'column_cache.inc'):
        return data, []
    text = data.decode('utf-8')
    recipe = []
    if '\r\n' in text:
        recipe.append(dict(operation='replace', before='\r\n', after='\n', count=text.count('\r\n')))
        text = text.replace('\r\n', '\n')
    if name == 'column_store.inc':
        text = replace_once(text,
            '\tif(!column_valid(cx,cz)) { streamError=1; return 0; }\n'
            '\tstreamError=0;\n'
            '\treturn 1;\n'
            '}\n',
            '\tif(!column_valid(cx,cz)) { streamError=1; return 0; }\n'
            '\tstreamError=0;\n'
            '\treturn 1;\n'
            '}\n\n'
            '/* Stream-only read: cache_load() validates these exact bytes before\n'
            '   installing them, so a second checksum here would be redundant. */\n'
            'static int column_prepare_unchecked(int cx,int cz)\n'
            '{\n'
            '\tint id=cz*g_NC+cx;\n'
            '\tColumnRecord *r=&columnRecords[id];\n'
            '\tif(r->disk && preparedColumn!=id)\n'
            '\t{\n'
            '\t\tint status=column_read_record(r);\n'
            '\t\tif(status!=1) { if(status<0) streamError=1; return 0; }\n'
            '\t\tpreparedColumn=id;\n'
            '\t}\n'
            '\tif(!column_bytes(id)) { streamError=1; return 0; }\n'
            '\tstreamError=0;\n'
            '\treturn 1;\n'
            '}\n', recipe)
    elif 'int ok=column_prepare(cx,cz);' in text:
        # Benchmark-only collect/world profile wraps the stream call.
        text = replace_once(text, 'int ok=column_prepare(cx,cz);',
                            'int ok=column_prepare_unchecked(cx,cz);', recipe)
    else:
        text = replace_once(text,
            '\t\tif(slot<0 || !column_prepare(bx,bz) || !cache_load(bx,bz,slot)) return;\n',
            '\t\tif(slot<0 || !column_prepare_unchecked(bx,bz) || !cache_load(bx,bz,slot)) return;\n',
            recipe)
    return text.encode('utf-8'), recipe
