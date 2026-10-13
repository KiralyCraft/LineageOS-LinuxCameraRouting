#!/usr/bin/env python3
"""Isolated real-camera Firefox resize regression; logs/profile live in RAM.

Requires Firefox with Marionette and strace. Does not use the user profile,
contact a website, save images, or change the Android helper. Run unprivileged
with access to the Linux Camera nodes; prints the temporary evidence directory.
"""
import os,pathlib,socket,json,subprocess,time,threading,http.server,tempfile,signal
out=pathlib.Path(tempfile.mkdtemp(prefix='camera-firefox-',dir='/tmp')); print('EVIDENCE',out,flush=True)
profile=out/'profile';profile.mkdir()
with socket.socket() as reserve:
 reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
prefs={'marionette.port':port,'media.navigator.permission.disabled':True,'media.navigator.streams.fake':False,'media.autoplay.default':0,'browser.shell.checkDefaultBrowser':False,'browser.startup.homepage_override.mstone':'ignore','datareporting.policy.dataSubmissionEnabled':False}
(profile/'user.js').write_text('\n'.join('user_pref('+json.dumps(k)+', '+json.dumps(v)+');' for k,v in prefs.items()))
class Handler(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(b'<video autoplay muted id="v"></video>')
 def log_message(self,*args):pass
server=http.server.HTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
log=(out/'firefox.log').open('w');env=dict(os.environ,MOZ_LOG='CamerasParent:5,MediaManager:4,VideoEngine:5',MOZ_HEADLESS='1')
proc=subprocess.Popen(['strace','-f','-yy','-e','trace=ioctl','-o',str(out/'ioctls.log'),'/usr/lib/firefox/firefox','--headless','--no-remote','--profile',str(profile),'--marionette','about:blank'],stdout=log,stderr=subprocess.STDOUT,env=env,start_new_session=True)
sock=None
try:
 for i in range(100):
  try:sock=socket.create_connection(('127.0.0.1',port),timeout=1);break
  except OSError:
   if proc.poll() is not None:raise RuntimeError('Firefox exited '+str(proc.returncode))
   time.sleep(.1)
 if sock is None:raise RuntimeError('No Marionette')
 sock.settimeout(45)
 def recv():
  n=b''
  while not n.endswith(b':'):
   chunk=sock.recv(1)
   if not chunk:raise EOFError('Marionette closed')
   n+=chunk
  size=int(n[:-1]);data=b''
  while len(data)<size:
   chunk=sock.recv(size-len(data))
   if not chunk:raise EOFError('Marionette closed')
   data+=chunk
  return json.loads(data)
 print('HELLO',recv(),flush=True);serial=0
 def call(method,args={}):
  global serial
  serial+=1;payload=json.dumps([0,serial,method,args]).encode();sock.sendall(str(len(payload)).encode()+b':'+payload)
  reply=recv()
  if reply[2]:raise RuntimeError(reply[2])
  return reply[3]
 print(call('WebDriver:NewSession',{}),flush=True)
 call('WebDriver:SetTimeouts',{'script':35000})
 call('WebDriver:Navigate',{'url':f'http://127.0.0.1:{server.server_port}/'})
 def js(s):
  reply=call('WebDriver:ExecuteAsyncScript',{'script':'const done=arguments[arguments.length-1];(async()=>{'+s+'})().then(done,e=>done({error:String(e),stack:e.stack}));','args':[],'newSandbox':False,'sandbox':None})
  value=reply['value']
  if isinstance(value,dict) and 'error' in value:raise RuntimeError(value)
  return value
 print('BOOTSTRAP',js('window.v=document.getElementById("v");let s=await navigator.mediaDevices.getUserMedia({video:true,audio:false});s.getTracks().forEach(t=>t.stop());return true;'),flush=True)
 print('DEVICES',js('return (await navigator.mediaDevices.enumerateDevices()).filter(d=>d.kind==="videoinput").map(d=>({id:d.deviceId,label:d.label}));'),flush=True)
 for lens in ['Front','Rear 6.08']:
  print('OPEN '+lens,js('let ds=await navigator.mediaDevices.enumerateDevices();let d=ds.find(d=>d.kind==="videoinput"&&d.label.includes('+json.dumps(lens)+'));window.stream=await navigator.mediaDevices.getUserMedia({video:{deviceId:{exact:d.deviceId},width:{exact:640},height:{exact:480},frameRate:30},audio:false});v.srcObject=stream;await v.play();await new Promise(r=>setTimeout(r,2500));return {settings:stream.getVideoTracks()[0].getSettings(),time:v.currentTime,width:v.videoWidth,height:v.videoHeight,decoded:v.mozDecodedFrames,quality:v.getVideoPlaybackQuality()};'),flush=True)
  resized=js('let t=stream.getVideoTracks()[0];await t.applyConstraints({width:{exact:1920},height:{exact:1080},frameRate:30});await new Promise(r=>setTimeout(r,3000));return {settings:t.getSettings(),time:v.currentTime,width:v.videoWidth,height:v.videoHeight,decoded:v.mozDecodedFrames,quality:v.getVideoPlaybackQuality(),state:t.readyState};')
  print('RESIZE '+lens,resized,flush=True)
  assert resized['settings']['width']==1920 and resized['settings']['height']==1080
  assert resized['width']==1920 and resized['height']==1080
  print('CHECK '+lens,js('await new Promise(r=>setTimeout(r,2000));return {time:v.currentTime,width:v.videoWidth,height:v.videoHeight,decoded:v.mozDecodedFrames,quality:v.getVideoPlaybackQuality(),state:stream.getVideoTracks()[0].readyState};'),flush=True)
  print('STOP',js('stream.getTracks().forEach(t=>t.stop());v.srcObject=null;return true;'),flush=True)
 print('PASS front/rear 640x480 -> 1920x1080 and camera switching; inspect DQBUF sequences for continued delivery',flush=True)
 call('Marionette:Quit',{'flags':['eForceQuit']})
finally:
 if sock:
  try:
   sock.settimeout(3);call('Marionette:Quit',{'flags':['eForceQuit']})
  except Exception:pass
  sock.close()
 if proc.poll() is None:
  os.killpg(proc.pid,signal.SIGTERM)
  try:proc.wait(timeout=8)
  except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGKILL);proc.wait()
 server.shutdown();log.close()
