<?php
declare(strict_types=1);
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');
function fail(int $code): never { http_response_code($code); echo '{"error":"Public wallet directory temporarily unavailable"}'; exit; }
$method=$_SERVER['REQUEST_METHOD'] ?? 'GET';
if (!in_array($method,['GET','POST'],true)) fail(405);
$action=$_GET['action'] ?? '';
if (!in_array($action,['','challenge','update'],true) || ($method==='GET' && $action!=='') || ($method==='POST' && $action==='')) fail(400);
$q=$_GET['q'] ?? '';
if (!is_string($q) || strlen($q)>100) fail(400);
$config=dirname($_SERVER['DOCUMENT_ROOT']).'/monzero-private/profile-proxy-token';
$token=@file_get_contents($config);
if (!$token || strlen(trim($token))<32) fail(503);
$body=$method==='POST' ? file_get_contents('php://input',false,null,0,4097) : '';
if (strlen($body)>4096) fail(413);
$c=curl_init('https://pool.monzero.org/wallet-profiles-api/'.($action!=='' ? $action : '?q='.rawurlencode($q)));
curl_setopt_array($c,[CURLOPT_RETURNTRANSFER=>true,CURLOPT_CONNECTTIMEOUT=>5,CURLOPT_TIMEOUT=>12,CURLOPT_FOLLOWLOCATION=>false,CURLOPT_PROTOCOLS=>CURLPROTO_HTTPS,CURLOPT_HTTPHEADER=>['Content-Type: application/json','X-Profile-Proxy: '.trim($token),'X-Profile-Client: '.($_SERVER['REMOTE_ADDR'] ?? 'unknown')]]);
if ($method==='POST') curl_setopt_array($c,[CURLOPT_POST=>true,CURLOPT_POSTFIELDS=>$body]);
$response=curl_exec($c);$status=curl_getinfo($c,CURLINFO_RESPONSE_CODE);curl_close($c);
if ($response===false || !in_array($status,[200,400,403,404,503],true) || strlen($response)>65536) fail(503);
http_response_code($status);echo $response;
