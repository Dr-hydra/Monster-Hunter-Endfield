"""Read Capcom's public PC manual through its normal no-age-input region form."""
from pathlib import Path
import http.cookiejar
import json
import re
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
BASE = 'https://game.capcom.com/manual/MHW_PC/'


def icon_text(html):
    body = re.search(r'<main\b[^>]*>(.*?)</main>', html, re.S)
    if not body:
        raise ValueError('Manual main content unavailable')
    html = body.group(1)
    def icon(match):
        source = re.search(r'src="([^"]+)"', match.group())
        return ' [' + source.group(1).split('/')[-1] + '] ' if source else ''
    html = re.sub(r'<img\b[^>]*>', icon, html)
    html = re.sub(r'</(?:p|li|h[1-6]|tr)>', '\n', html)
    html = re.sub('<[^>]+>', ' ', html)
    return '\n'.join(re.sub(r'\s+', ' ', row).strip() for row in html.splitlines() if row.strip())


def main():
    out = ROOT / 'research/keyboard-defaults'
    out.mkdir(parents=True, exist_ok=True)
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
    opener.open(BASE, timeout=25).read()
    request = urllib.request.Request(BASE + 'en/steam/validate', data=b'region=gb', headers={'Referer': BASE})
    opener.open(request, timeout=25).read()
    records = []
    for page in ('1/4', '2/1', '2/2', '8/1', '9/1', '19/1', '19/2'):
        url = BASE + 'en/steam/page/' + page
        response = opener.open(url, timeout=25)
        raw = response.read()
        name = 'page-' + page.replace('/', '-')
        (out / (name + '.html')).write_bytes(raw)
        text = icon_text(raw.decode('utf-8'))
        (out / (name + '.icon-text.txt')).write_text(text, encoding='utf-8')
        records.append({'url': url, 'resolved_url': response.url, 'file': name + '.html', 'bytes': len(raw)})
        print(page, len(raw), flush=True)
    (out / 'primary-sources.json').write_text(json.dumps(records, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
