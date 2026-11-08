"""Compile both production caret-extent handlers against a fake TSF context view"""

from pathlib import Path
import sys

from generate_reconnect_fixture import extract_body


def generate(layout_path, edit_path, layout_header_path, ime_path, output_path):
    layout = Path(layout_path).read_text(encoding='utf-8')
    edit = Path(edit_path).read_text(encoding='utf-8')
    header = Path(layout_header_path).read_text(encoding='utf-8')
    ime = Path(ime_path).read_text(encoding='utf-8')
    generated = '// Generated from the production caret-extent handlers; do not edit\n'
    signature = ('bool CMetasequoiaIME::_IsCandidateLayoutCurrent('
                 'const CTfTextLayoutSink *layout, uint64_t compositionEpoch) const')
    generated += signature + ' ' + extract_body(ime, 'bool CMetasequoiaIME::_IsCandidateLayoutCurrent(') + '\n'
    generated += 'HRESULT CTfTextLayoutSink::_GetTextExt(RECT *lpRect, POINT *lpAnchor) '
    generated += extract_body(layout, 'HRESULT CTfTextLayoutSink::_GetTextExt(') + '\n'
    generated += 'bool CTfTextLayoutSink::_IsLayoutCurrent(ITfContext *context, ITfRange *range) const '
    generated += extract_body(header, 'bool _IsLayoutCurrent(') + '\n'
    generated += ('CGetTextExtentEditSession::CGetTextExtentEditSession('
                  'CMetasequoiaIME *pTextService, ITfContext *pContext, ITfContextView *pContextView, '
                  'ITfRange *pRangeComposition, CTfTextLayoutSink *pTfTextLayoutSink) '
                  ': CEditSessionBase(pTextService, pContext) ')
    generated += extract_body(edit, 'CGetTextExtentEditSession::CGetTextExtentEditSession(') + '\n'
    generated += 'CGetTextExtentEditSession::~CGetTextExtentEditSession() '
    generated += extract_body(edit, 'CGetTextExtentEditSession::~CGetTextExtentEditSession()') + '\n'
    generated += 'HRESULT CGetTextExtentEditSession::DoEditSession(TfEditCookie ec) '
    generated += extract_body(edit, 'STDAPI CGetTextExtentEditSession::DoEditSession(') + '\n'
    Path(output_path).write_text(generated, encoding='utf-8')


if __name__ == '__main__':
    generate(*sys.argv[1:])
