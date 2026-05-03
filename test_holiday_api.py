#!/usr/bin/env python3
"""
test_holiday_api.py

A small test utility to exercise the Festivo public holidays API and the
local `/holiday` endpoints provided by this project.

Usage examples:
  python test_holiday_api.py --api-key YOUR_KEY            # test external API only
  python test_holiday_api.py --api-key YOUR_KEY --local https://127.0.0.1:443 --username aaron --password aaron123

The script uses only the Python standard library (urllib) so no extra deps.
"""

import argparse
import json
import sys
import urllib.request
import urllib.parse
import http.cookiejar
import ssl
from datetime import datetime, date

FESTIVO_BASE = "https://api.getfestivo.com/public-holidays/v3/list"


def safe_json_loads(s):
    try:
        return json.loads(s)
    except Exception:
        return None


def fetch_festivo(api_key, country='US', year=None, month=None, day=None, use_header=False, timeout=15):
    params = {'api_key': api_key, 'country': country}
    if year is not None:
        params['year'] = str(year)
    if month is not None:
        params['month'] = str(month)
    if day is not None:
        params['day'] = str(day)
    url = FESTIVO_BASE + '?' + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers={
        'Accept': 'application/json',
        'User-Agent': 'HolidayTest/1.0'
    })
    if use_header:
        req.add_header('X-API-Key', api_key)
    ctx = ssl.create_default_context()
    with urllib.request.urlopen(req, context=ctx, timeout=timeout) as resp:
        body = resp.read().decode('utf-8')
        return resp.getcode(), safe_json_loads(body)


class LocalClient:
    def __init__(self, base_url, verify_ssl=False, timeout=15):
        self.base_url = base_url.rstrip('/')
        self.timeout = timeout
        self.cookiejar = http.cookiejar.CookieJar()
        handlers = []
        if not verify_ssl:
            ctx = ssl._create_unverified_context()
            handlers.append(urllib.request.HTTPSHandler(context=ctx))
        handlers.append(urllib.request.HTTPCookieProcessor(self.cookiejar))
        handlers.append(urllib.request.HTTPRedirectHandler())
        self.opener = urllib.request.build_opener(*handlers)
        self.opener.addheaders = [('User-Agent', 'HolidayTest/1.0')]

    def _url(self, path):
        if path.startswith('/'):
            path = path[1:]
        return self.base_url + '/' + path

    def post_form(self, path, data):
        url = self._url(path)
        body = urllib.parse.urlencode(data).encode('utf-8')
        req = urllib.request.Request(url, data=body, headers={'Content-Type': 'application/x-www-form-urlencoded'})
        with self.opener.open(req, timeout=self.timeout) as resp:
            return resp.getcode(), resp.read().decode('utf-8')

    def get(self, path, params=None):
        url = self._url(path)
        if params:
            url = url + '?' + urllib.parse.urlencode(params)
        req = urllib.request.Request(url)
        with self.opener.open(req, timeout=self.timeout) as resp:
            return resp.getcode(), resp.read().decode('utf-8')


def parse_args():
    p = argparse.ArgumentParser(description='Test Festivo and local holiday API endpoints')
    p.add_argument('--api-key', '-k', help='Festivo API key', required=False)
    p.add_argument('--country', default='US', help='Country ISO code (default US)')
    p.add_argument('--local', default=None, help='Local server base URL (https://127.0.0.1:443)')
    p.add_argument('--username', default='aaron', help='Local login username')
    p.add_argument('--password', default='aaron123', help='Local login password')
    p.add_argument('--save-key', action='store_true', help='Save API key to local account (.festivo_key)')
    p.add_argument('--as-of', help='Optional YYYY-MM-DD to use as the start date for finding next holiday')
    p.add_argument('--skip-external', action='store_true', help='Skip calling the external Festivo API')
    p.add_argument('--use-header', action='store_true', help='Send API key as X-API-Key header in external request')
    p.add_argument('--archival', action='store_true', help='Only test archival years (e.g. 2025)')
    return p.parse_args()


def main():
    args = parse_args()

    api_key = args.api_key
    country = args.country

    as_of_date = None
    if args.as_of:
        try:
            as_of_date = datetime.strptime(args.as_of, '%Y-%m-%d').date()
        except Exception as e:
            print('Invalid --as-of date, use YYYY-MM-DD')
            return 2

    if not args.skip_external:
        if not api_key:
            print('Skipping external Festivo test (no API key provided)')
        else:
            print('Testing external Festivo API...')
            # Default to current year when not provided; if --as-of supplied, use that date.
            if as_of_date:
                y = as_of_date.year
                m = as_of_date.month
                d = as_of_date.day
            else:
                now = datetime.utcnow()
                y = now.year
                m = None
                d = None

            archival_years = [2025, 2024, 2023, 2022, 2021]

            def try_fetch(year, month=None, day=None):
                try:
                    code, body = fetch_festivo(api_key, country=country, year=year, month=month, day=day, use_header=args.use_header)
                    return code, body, None
                except urllib.error.HTTPError as he:
                    try:
                        body_text = he.read().decode('utf-8')
                        body = safe_json_loads(body_text)
                        return he.code, body, body_text
                    except Exception:
                        return he.code, None, None
                except Exception as e:
                    return None, None, str(e)

            # If user requested archival only, iterate archival years first
            if args.archival:
                found = False
                for ay in archival_years:
                    code, body, raw = try_fetch(ay)
                    print(f'Trying archival year {ay} -> status {code}')
                    if code == 200 and isinstance(body, dict) and body.get('holidays'):
                        print('Found', len(body['holidays']), 'holidays in response')
                        print('First holiday:', body['holidays'][0].get('name'), body['holidays'][0].get('date'))
                        found = True
                        break
                if not found:
                    print('No archival holidays found for tested years')
            else:
                # normal flow: try supplied/as-of/current year, fallback to archival on 402 or empty
                code = None; body = None; raw = None
                if as_of_date:
                    code, body, raw = try_fetch(as_of_date.year, as_of_date.month, as_of_date.day)
                    print('Tried as-of date -> status', code)
                else:
                    now = datetime.utcnow()
                    code, body, raw = try_fetch(now.year)
                    print('Tried current year -> status', code)

                if code == 200 and isinstance(body, dict) and body.get('holidays'):
                    print('Found', len(body['holidays']), 'holidays in response')
                    if len(body['holidays']) > 0:
                        h = body['holidays'][0]
                        print('First holiday:', h.get('name'), h.get('date'))
                else:
                    # If payment_required or no holidays, try archival years as fallback
                    need_archival = False
                    if code == 402:
                        need_archival = True
                    if isinstance(body, dict) and (not body.get('holidays')):
                        need_archival = True
                    if raw and 'payment_required' in raw:
                        need_archival = True

                    if need_archival:
                        print('Falling back to archival years due to API plan/response')
                        found = False
                        for ay in archival_years:
                            code, body, raw = try_fetch(ay)
                            print(f'Trying archival year {ay} -> status {code}')
                            if code == 200 and isinstance(body, dict) and body.get('holidays'):
                                print('Found', len(body['holidays']), 'holidays in response')
                                print('First holiday:', body['holidays'][0].get('name'), body['holidays'][0].get('date'))
                                found = True
                                break
                        if not found:
                            print('No archival holidays found for tested years')
                    else:
                        print('Status:', code)
                        if body is None:
                            print('No JSON response')
                        else:
                            print('Response JSON (partial):')
                            print(json.dumps(body, indent=2)[:1000])

    if args.local:
        print('\nTesting local server at', args.local)
        client = LocalClient(args.local, verify_ssl=False)
        # login
        print('Logging in as', args.username)
        try:
            code, body = client.post_form('/login', {'username': args.username, 'password': args.password})
            print('Login HTTP status:', code)
        except Exception as e:
            print('Login failed:', e)
            return 3

        # show cookies
        sess_token = None
        for c in client.cookiejar:
            if c.name == 'session_token':
                sess_token = c.value
        print('Session token present:' , bool(sess_token))

        # get saved key
        try:
            code, body = client.get('/holiday/key')
            print('/holiday/key status:', code)
            j = safe_json_loads(body)
            if j and 'api_key' in j:
                print('Saved key (local):', '(present)' if j.get('api_key') else '(empty)')
        except Exception as e:
            print('Failed to get /holiday/key:', e)

        # optionally save key
        if api_key and args.save_key:
            print('Saving API key to local account...')
            try:
                code, body = client.post_form('/holiday/key', {'api_key': api_key})
                print('/holiday/key POST status:', code)
                print('Response:', body)
            except Exception as e:
                print('Failed to save key:', e)

        # call /holiday/next
        params = {'country': country}
        if as_of_date:
            params['year'] = str(as_of_date.year)
            params['month'] = str(as_of_date.month)
            params['day'] = str(as_of_date.day)
        print('Requesting /holiday/next with params:', params)
        try:
            code, body = client.get('/holiday/next', params=params)
            print('/holiday/next status:', code)
            j = safe_json_loads(body)
            if j is None:
                print('Response body (raw):')
                print(body[:1000])
            else:
                if 'error' in j:
                    print('Error from server:', j['error'])
                else:
                    name = j.get('name')
                    dstr = j.get('date')
                    print('Server returned next holiday:', name, dstr)
                    if dstr:
                        try:
                            d = datetime.strptime(dstr, '%Y-%m-%d').date()
                            ref = as_of_date or date.today()
                            print('Days from', ref.isoformat(), ':', (d - ref).days)
                        except Exception:
                            pass
        except Exception as e:
            print('Failed to call /holiday/next:', e)
            return 4

    print('\nDone')
    return 0


if __name__ == '__main__':
    sys.exit(main())
