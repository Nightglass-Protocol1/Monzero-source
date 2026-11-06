#!/usr/bin/env python3
"""Archive dated public node observations, not a canonical blockchain index."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import sqlite3
import stat
import subprocess
import sys
import time
import tempfile
import urllib.request

SOURCES={'live':'https://monzero.org/stats/api.php','preview':'http://127.0.0.1:8765/stats/api.php'}
MAX_SAFE=9007199254740991
RANGES={'24h':86400,'7d':604800,'30d':2592000,'90d':7776000,'1y':31536000,'all':None}

def integer(value,minimum=0):
    return type(value) is int and minimum<=value<=MAX_SAFE

def normalize(data,now):
    if not isinstance(data,dict) or data.get('schema')!='monzero-chain-charts-v2' or data.get('network')!='mainnet' or type(data.get('target_seconds')) is not int or data.get('target_seconds')!=120:
        raise ValueError('Unsupported mainnet observation')
    observed=data.get('generated_at');height=data.get('height');blocks=data.get('blocks')
    if not integer(observed,1) or not integer(height,2) or type(data.get('synchronized')) is not bool:
        raise ValueError('Invalid observation metadata')
    if observed>now:raise ValueError('Source clock is ahead of collector; check system clocks')
    if now-observed>300:raise ValueError('Observation is older than five minutes')
    start=max(1,height-720)
    if not isinstance(blocks,list) or len(blocks)!=height-start:
        raise ValueError('Incomplete source window')
    for index,block in enumerate(blocks):
        if not isinstance(block,dict) or block.get('height')!=start+index or not integer(block.get('height'),1) or not integer(block.get('timestamp'),1) or not integer(block.get('difficulty'),1) or not integer(block.get('transactions')):
            raise ValueError('Invalid block sample')
    tip=blocks[-1]
    # Retain only the tip observation; do not duplicate 720-block windows each minute.
    return {'observed_at':observed,'height':height,'tip_timestamp':tip['timestamp'],
            'difficulty':tip['difficulty'],'synchronized':data['synchronized']}

def connect(path,readonly=False):
    path=Path(path).absolute();parent=path.parent
    if not readonly:parent.mkdir(mode=0o700,parents=True,exist_ok=True)
    if parent.is_symlink() or parent.stat().st_mode&0o077 or parent.stat().st_uid!=os.geteuid():
        raise ValueError('Database directory must be private and owned by this user')
    if path.is_symlink():raise ValueError('Database must not be a symlink')
    fd=os.open(path,(os.O_RDONLY if readonly else os.O_CREAT|os.O_RDWR)|os.O_NOFOLLOW,0o600)
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):raise ValueError('Database must be a regular file')
        if os.fstat(fd).st_uid!=os.geteuid():raise ValueError('Database owner mismatch')
        if readonly:
            if os.fstat(fd).st_mode&0o077:raise ValueError('Database must be private')
        else:os.fchmod(fd,0o600)
    finally:os.close(fd)
    db=sqlite3.connect(path.as_uri()+'?mode=ro',uri=True,timeout=5) if readonly else sqlite3.connect(path,timeout=5)
    try:
        db.execute('PRAGMA foreign_keys=ON')
        db.execute('BEGIN' if readonly else 'BEGIN IMMEDIATE')
        version=db.execute('PRAGMA user_version').fetchone()[0]
        if (readonly and version!=1) or version not in (0,1):raise ValueError('Unsupported history schema version')
        tables=[r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name")]
        columns=[r[1] for r in db.execute('PRAGMA table_info(observations)')]
        expected=['observed_at','height','tip_timestamp','difficulty','synchronized','source','digest']
        if (version==0 and tables) or (version==1 and (tables!=['observations'] or columns!=expected)):
            raise ValueError('Unrecognized database; no migration performed')
        with db:
            if version==0:
                db.execute('CREATE TABLE observations (observed_at INTEGER PRIMARY KEY, height INTEGER NOT NULL CHECK(height>=2), tip_timestamp INTEGER NOT NULL, difficulty TEXT NOT NULL, synchronized INTEGER NOT NULL CHECK(synchronized IN (0,1)), source TEXT NOT NULL, digest TEXT NOT NULL)')
                db.execute('PRAGMA user_version=1')
        return db
    except BaseException:db.close();raise

def record(db,data,source,now):
    if source not in SOURCES:raise ValueError('Unknown source')
    value=normalize(data,now)
    digest=hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    with db:
        db.execute('BEGIN IMMEDIATE')
        existing_sources={row[0] for row in db.execute('SELECT DISTINCT source FROM observations')}
        if existing_sources and existing_sources!={source}:raise ValueError('Do not mix observation sources in one archive')
        old=db.execute('SELECT digest,source FROM observations WHERE observed_at=?',(value['observed_at'],)).fetchone()
        if old:
            if old!=(digest,source):raise ValueError('Conflicting observation at the same time')
            return False
        latest=db.execute('SELECT max(observed_at) FROM observations').fetchone()[0]
        if latest is not None and value['observed_at']<latest:raise ValueError('Observation clock moved backwards')
        # One collector observation per minute bounds long-term growth.
        if latest is not None and value['observed_at']-latest<60:return False
        db.execute('INSERT INTO observations VALUES (?,?,?,?,?,?,?)',
                   (value['observed_at'],value['height'],value['tip_timestamp'],str(value['difficulty']),int(value['synchronized']),source,digest))
    return True

def export(db,range_name,now):
    db.execute('SAVEPOINT history_export')
    try:return _export(db,range_name,now)
    finally:db.execute('RELEASE history_export')

def _export(db,range_name,now):
    if range_name not in RANGES:raise ValueError('Unknown range')
    sources=[r[0] for r in db.execute('SELECT DISTINCT source FROM observations ORDER BY source')]
    if len(sources)>1:raise ValueError('Mixed-source archive cannot be exported')
    first,last,total=db.execute('SELECT min(observed_at),max(observed_at),count(*) FROM observations').fetchone()
    duration=RANGES[range_name]
    start=max(0,now-duration) if duration is not None else (first if first is not None else now)
    # At most 721 fixed time buckets, regardless of archive length.
    bucket=max(60,math.ceil(max(1,now-start)/720))
    anchor=(start//bucket)*bucket
    rows=db.execute('SELECT CAST((observed_at-?)/? AS INTEGER),min(observed_at),max(observed_at),count(*),avg(CAST(difficulty AS REAL)),min(CAST(difficulty AS INTEGER)),max(CAST(difficulty AS INTEGER)),min(height),max(height),min(synchronized),sum(synchronized),min(observed_at-tip_timestamp),max(observed_at-tip_timestamp) FROM observations WHERE observed_at>=? AND observed_at<=? GROUP BY 1 ORDER BY 1',(anchor,bucket,start,now)).fetchall()
    points=[{'first_observed_at':r[1],'last_observed_at':r[2],'samples':r[3],
             'mean_difficulty':r[4],'mean_estimated_hashrate_hps':r[4]/120,
             'minimum_difficulty':str(r[5]),'maximum_difficulty':str(r[6]),
             'minimum_height':r[7],'maximum_height':r[8],'all_samples_synchronized':bool(r[9]),
             'synchronized_samples':r[10],'minimum_tip_age_seconds':r[11],'maximum_tip_age_seconds':r[12]} for r in rows]
    return {'schema':'monzero-network-history-v1','network':'mainnet','generated_at':now,'range':range_name,
            'archive_first_observed_at':first,'archive_last_observed_at':last,'archive_samples':total,
            'sources':sources,
            'bucket_seconds':bucket,'points':points,
            'limitations':['Dated observations from the configured public node, not independent consensus verification.',
             'Difficulty and estimated hashrate are sampled at the observed tip; averages are per observation, not time-weighted.',
             'Unsynchronized and stale-tip observations are retained; per-bucket sync counts and tip ages qualify the averages.',
             'Negative tip ages mean the block timestamp is ahead of the observation clock.',
             'Missing periods are not filled. A bucket does not imply continuous monitoring.',
             'Recorded observations are not rewritten after reorganizations; height can decrease.',
             'Emission, circulating supply, node counts, software versions and block interval distributions are not collected here.']}

def publish(db,directory,now):
    directory=Path(directory)
    if directory.is_symlink():raise ValueError('Export directory must not be a symlink')
    directory.mkdir(parents=True,exist_ok=True)
    db.execute('SAVEPOINT history_publish')
    try:exports={name:export(db,name,now) for name in RANGES}
    finally:db.execute('RELEASE history_publish')
    for name,data in exports.items():
        temporary=None
        try:
            with tempfile.NamedTemporaryFile(mode='w',encoding='utf-8',dir=directory,prefix='.history-',delete=False) as file:
                temporary=Path(file.name)
                json.dump(data,file,allow_nan=False,separators=(',',':'))
                file.flush();os.fsync(file.fileno());os.fchmod(file.fileno(),0o644)
            os.replace(temporary,directory/(name+'.json'))
        finally:
            if temporary is not None:temporary.unlink(missing_ok=True)
    # Files are individually atomic. Each carries its own generation timestamp.
    return len(exports)

# Isolate transport so a trickling server cannot extend collection indefinitely.
_FETCH_PROGRAM = """
import sys, urllib.request
class NoRedirect(urllib.request.HTTPRedirectHandler):
 def redirect_request(self,*args,**kwargs):return None
with urllib.request.build_opener(NoRedirect).open(sys.argv[1],timeout=10) as response:
 raw=response.read(2097153)
 if len(raw)>2097152:raise ValueError('Observation too large')
 sys.stdout.buffer.write(raw)
"""
def fetch(source,timeout=12):
    if source not in SOURCES:raise ValueError('Unknown source')
    try:
        result=subprocess.run([sys.executable,'-I','-c',_FETCH_PROGRAM,SOURCES[source]],capture_output=True,timeout=timeout,check=False)
    except subprocess.TimeoutExpired as error:raise ValueError('Observation request timed out') from error
    if result.returncode:raise ValueError('Observation request failed')
    return json.loads(result.stdout)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database',required=True)
    parser.add_argument('--source',choices=SOURCES,default='live')
    parser.add_argument('--range',choices=RANGES,default='24h')
    parser.add_argument('--output-directory',help='Atomically export all ranges as public JSON files')
    parser.add_argument('--export-only',action='store_true',help='Export stored observations without contacting the source')
    args=parser.parse_args();data=None if args.export_only else fetch(args.source);now=int(time.time())
    db=connect(args.database,readonly=args.export_only)
    try:
        if data is not None:
            stored=record(db,data,args.source,now)
            print('Observation stored' if stored else 'Observation already stored or skipped by minute cadence',file=sys.stderr)
        if args.output_directory:
            count=publish(db,args.output_directory,int(time.time()));print(f'Published {count} range files',file=sys.stderr)
        else:print(json.dumps(export(db,args.range,int(time.time())),allow_nan=False,separators=(',',':')))
    finally:db.close()
if __name__=='__main__':main()
