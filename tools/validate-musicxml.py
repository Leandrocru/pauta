"""Validates MusicXML files against the official XSD.
Usage: python tools/validate-musicxml.py <folder with musicxml.xsd, xlink.xsd, xml.xsd> <file or glob>...
(The schema files come from https://github.com/w3c/musicxml, folder schema/.)"""
import sys, os, glob
from lxml import etree

xsd_dir = sys.argv[1]
main = os.path.join(xsd_dir, 'musicxml.xsd')
text = open(main, encoding='utf-8').read()
text = text.replace('http://www.musicxml.org/xsd/xml.xsd', 'xml.xsd').replace('http://www.musicxml.org/xsd/xlink.xsd', 'xlink.xsd')
local = os.path.join(xsd_dir, 'musicxml-local.xsd')
open(local, 'w', encoding='utf-8').write(text)
schema = etree.XMLSchema(etree.parse(local))

ok = True
for pattern in sys.argv[2:]:
    for f in sorted(glob.glob(pattern)):
        parser = etree.XMLParser(load_dtd=False, no_network=True, resolve_entities=False)
        doc = etree.parse(f, parser)
        if schema.validate(doc):
            print(f"VALIDO    {os.path.basename(f)}")
        else:
            ok = False
            print(f"INVALIDO  {os.path.basename(f)}")
            for err in list(schema.error_log)[:6]:
                print(f"   linha {err.line}: {err.message}")
sys.exit(0 if ok else 1)
