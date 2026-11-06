// Recent block statistics remain separate from sampled historical tip observations.
const integer=(n,min=0)=>Number.isSafeInteger(n)&&n>=min;
export function summarizeBlocks(data){
 const bad=()=>{throw Error('Invalid recent block observation');};
 if(!data||data.schema!=='monzero-chain-charts-v2'||data.network!=='mainnet'||data.target_seconds!==120||!integer(data.generated_at,1)||data.generated_at>253402300799||!integer(data.height,2)||typeof data.synchronized!=='boolean'||!Array.isArray(data.blocks))bad();
 const start=Math.max(1,data.height-720),blocks=data.blocks;
 if(blocks.length!==data.height-start)bad();
 for(let i=0;i<blocks.length;i++){
  const b=blocks[i];
  if(!b||b.height!==start+i||!integer(b.timestamp,1)||b.timestamp>253402300799||!integer(b.difficulty,1)||!integer(b.transactions))bad();
 }
 const bins=[{label:'0–59 seconds',count:0},{label:'60–119 seconds',count:0},{label:'120–239 seconds',count:0},{label:'240–599 seconds',count:0},{label:'600 seconds or more',count:0}];
 let backwards=0;
 for(let i=1;i<blocks.length;i++){
  const interval=blocks[i].timestamp-blocks[i-1].timestamp;
  if(interval<0){backwards++;continue;}
  bins[interval<60?0:interval<120?1:interval<240?2:interval<600?3:4].count++;
 }
 const intervals=blocks.length-1,valid=intervals-backwards,span=blocks[blocks.length-1].timestamp-blocks[0].timestamp;
 // Endpoint-based estimate; internal timestamp reversals remain explicitly counted.
 return {tipDifficulty:blocks[blocks.length-1].difficulty,generatedAt:data.generated_at,height:data.height,synchronized:data.synchronized,firstHeight:start,lastHeight:data.height-1,blockCount:blocks.length,firstTimestamp:blocks[0].timestamp,lastTimestamp:blocks[blocks.length-1].timestamp,bins,backwards,validIntervals:valid,blocksPerHour:intervals>0&&span>0?intervals*3600/span:null};
}
