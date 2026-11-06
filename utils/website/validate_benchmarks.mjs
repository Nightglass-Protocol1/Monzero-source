// Uses the same validation contract as the public catalog renderer.
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {validateCatalog} from '../../website/mining/benchmarks/catalog.mjs';
const path=process.argv[2] ? resolve(process.argv[2]) : new URL('../../website/mining/benchmarks/catalog.json',import.meta.url);
try {
 const data=validateCatalog(JSON.parse(readFileSync(path,'utf8')));
 if(data.updated_at>new Date().toISOString().slice(0,10))throw new Error('Catalog date is in the future');
 console.log(`Validated ${data.records.length} benchmark records; catalog date ${data.updated_at}. Evidence still requires human review.`);
} catch(error) {
 console.error(`Benchmark validation failed: ${error.message}`);
 process.exitCode=1;
}
