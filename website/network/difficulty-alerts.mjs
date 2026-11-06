// Opt-in session model. The caller supplies validated recent-block summaries.
export function createDifficultyMonitor(percent){
 if(typeof percent!=='number'||!Number.isFinite(percent)||percent<1||percent>100)throw Error('Choose a change threshold from 1 to 100 percent.');
 let baseline=null,lastObserved=0,lastSample=-Infinity,lastClock=-Infinity,pending=null,count=0,latched=null,lastAlert=-Infinity,lastCountedObserved=0,reason='Waiting for an observation.';
 return {
  baseline:()=>baseline,
  status:()=>reason,
  sample(observation,wallSeconds,monotonicMs){
   if(!Number.isFinite(wallSeconds)||!Number.isFinite(monotonicMs)||monotonicMs<0||monotonicMs<lastClock)return null;
   lastClock=monotonicMs;
   const o=observation;
   reason=!o?'Lookup failed; confirmation reset.':o.generatedAt>wallSeconds?'Clock mismatch; observation is ahead of your clock.':wallSeconds-o.generatedAt>300?'Observation is stale; confirmation reset.':o.synchronized!==true?'Node is syncing; confirmation reset.':o.lastTimestamp>o.generatedAt?'Block timestamp is in the future; confirmation reset.':o.generatedAt-o.lastTimestamp>300?'Tip is over five minutes old; confirmation reset.':'Fresh observation accepted.';
   if(!o||!Number.isSafeInteger(o.generatedAt)||o.generatedAt<=0||o.generatedAt>wallSeconds||wallSeconds-o.generatedAt>300||o.synchronized!==true||!Number.isSafeInteger(o.tipDifficulty)||o.tipDifficulty<=0||!Number.isSafeInteger(o.lastTimestamp)||o.lastTimestamp<=0||o.lastTimestamp>o.generatedAt||o.generatedAt-o.lastTimestamp>300){pending=null;count=0;return null;}
   if(o.generatedAt<=lastObserved)return null;
   const previousObserved=lastObserved;lastObserved=o.generatedAt;
   if(baseline===null){baseline=o.tipDifficulty;lastSample=monotonicMs;lastCountedObserved=o.generatedAt;return null;}
   const change=(o.tipDifficulty-baseline)/baseline*100;
   const direction=change>=percent?'up':change<=-percent?'down':null;
   if(direction===null){pending=null;count=0;latched=null;return null;}
   if(pending!==null&&pending!==direction){pending=null;count=0;}
   if(monotonicMs-lastSample<30000||o.generatedAt-lastCountedObserved<30)return null;
   if(monotonicMs-lastSample>180000||o.generatedAt-previousObserved>180){pending=null;count=0;}
   lastSample=monotonicMs;lastCountedObserved=o.generatedAt;
   if(pending!==direction){pending=direction;count=0;}
   count++;
   if(count<2||latched===direction||monotonicMs-lastAlert<600000)return null;
   latched=direction;lastAlert=monotonicMs;
   return {direction,changePercent:change,baseline,difficulty:o.tipDifficulty,observedAt:o.generatedAt};
  }
 };
}
