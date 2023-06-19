#!/usr/bin/env python
import asyncio
import json
import logging
import os
import queue
import socket
import threading
import time
import itertools
import bisect
from collections import OrderedDict
from random import normalvariate

import serial
from aiohttp import web, WSCloseCode, WSMsgType
from aiohttp.web import json_response
from wand.image import Image
from wand.color import Color

logger = logging.getLogger(__name__)

# Edit this to match the serial port name shown in Arduino IDE
SERIAL_PORT = ""
HTTP_PORT = 5000
LOCAL_PROFILES_PATH = "/run/user/1000/gvfs/sftp:host=192.168.1.166/home/stepmania/.itgmania/Save/LocalProfiles"

# Event to tell the reader and writer threads to exit.
thread_stop_event = threading.Event()

# Amount of sensors.
num_sensors = 8

# Initialize sensor ids.
sensor_numbers = range(num_sensors)

# Used for developmental purposes. Set this to true when you just want to
# emulate the serial device instead of actually connecting to one.
NO_SERIAL = False


class ProfileHandler(object):
  """
  A class to handle all the profile modifications.

  Attributes:
    filename: string, the filename where to read/write profile data.
    profiles: OrderedDict, the profile data loaded from the file.
    cur_profile: string, the name of the current active profile.
    loaded: bool, whether or not the backend has already loaded the
      profile data file or not.
  """
  def __init__(self, filename='profiles.txt'):
    self.filename = filename
    self.profiles = OrderedDict()
    self.cur_profile = ''
    # Have a default no-name profile we can use in case there are no profiles.
    self.profiles[''] = {"image": "default.gif", "thresholds": [1000] * num_sensors}
    self.loaded = False

  def MaybeLoad(self):
    if not self.loaded:
      num_profiles = 0
      if os.path.exists(self.filename):
        with open(self.filename, 'r') as f:
          for line in f:
            profile = json.loads(line)
            self.profiles[profile["name"]] = profile["data"]
            num_profiles += 1
            # Change to the first profile found.
            # This will also emit the thresholds.
            if num_profiles == 1:
              self.ChangeProfile(profile["name"])
      else:
        open(self.filename, 'w').close()
      self.loaded = True
      print('Found Profiles: ' + str(list(self.profiles.keys())))

  def GetCurrent(self, key):
    if self.cur_profile not in self.profiles:
      # Should never get here assuming cur_profile is always appropriately
      # updated, but you never know.
      self.ChangeProfile('')
    return self.profiles[self.cur_profile][key]

  def UpdateCurrentAndBroadcast(self, key, value):
    if self.cur_profile not in self.profiles:
      # Should never get here assuming cur_profile is always appropriately
      # updated, but you never know.
      self.ChangeProfile('')
    self.profiles[self.cur_profile][key] = value
    self.Broadcast(key)
    self.UpdateProfiles()

  def Broadcast(self, key):
    if self.cur_profile in self.profiles:
      broadcast([key, {key: self.GetCurrent(key)}])
  
  def BroadcastAll(self):
    for key, _ in self.profiles[self.cur_profile].items():
      self.Broadcast(key)


  def UpdateThresholds(self, index, value):
    if self.cur_profile in self.profiles:
      new = self.GetCurrent("thresholds")
      new[index] = value
      self.UpdateCurrentAndBroadcast("thresholds", new)
      print('Thresholds are: ' + str(self.GetCurrent("thresholds")))

  def UpdateProfiles(self):
    with open(self.filename, 'w') as f:
      for name, data in self.profiles.items():
        if name:
          f.write(json.dumps({"name": name, "data": data}) + "\n")

  def ChangeProfile(self, profile_name):
    if profile_name in self.profiles:
      self.cur_profile = profile_name
      self.BroadcastAll()
      broadcast(['get_cur_profile', {'cur_profile': self.GetCurrentProfile()}])
      print('Changed to profile "{}" with thresholds: {}'.format(
        self.GetCurrentProfile(), str(self.GetCurrent("thresholds"))))

  def GetProfileNames(self):
    return [name for name in self.profiles.keys() if name]

  def AddProfile(self, profile_name, data):
    self.profiles[profile_name] = data
    if self.cur_profile == '':
      self.profiles[''] = {"image": None, "thresholds": [1000] * num_sensors}
    # ChangeProfile emits 'thresholds' and 'cur_profile'
    self.ChangeProfile(profile_name)
    self.UpdateProfiles()
    broadcast(['get_profiles', {'profiles': self.GetProfileNames()}])
    print('Added profile "{}" with thresholds: {}'.format(
      self.GetCurrentProfile(), str(self.GetCurrent("thresholds"))))

  def RemoveProfile(self, profile_name):
    if profile_name in self.profiles:
      del self.profiles[profile_name]
      if profile_name == self.cur_profile:
        self.ChangeProfile('')
      self.UpdateProfiles()
      broadcast(['get_profiles', {'profiles': self.GetProfileNames()}])
      broadcast(['get_cur_profile', {'cur_profile': self.GetCurrentProfile()}])
      print('Removed profile "{}". Current thresholds are: {}'.format(
        profile_name, str(self.GetCurrent("thresholds"))))

  def GetCurrentProfile(self):
    return self.cur_profile


class SerialHandler(object):
  """
  A class to handle all the serial interactions.

  Attributes:
    ser: Serial, the serial object opened by this class.
    port: string, the path/name of the serial object to open.
    timeout: int, the time in seconds indicating the timeout for serial
      operations.
    write_queue: Queue, a queue object read by the writer thread
    profile_handler: ProfileHandler, the global profile_handler used to update
      the thresholds
  """
  def __init__(self, profile_handler, port='', timeout=1):
    self.ser = None
    self.port = port
    self.timeout = timeout
    self.write_queue = queue.Queue(num_sensors + 10)
    self.profile_handler = profile_handler

    # Use this to store the values when emulating serial so the graph isn't too
    # jumpy. Only used when NO_SERIAL is true.
    self.no_serial_values = [0] * num_sensors

  def ChangePort(self, port):
    if self.ser:
      self.ser.close()
      self.ser = None
    self.port = port
    self.Open()

  def Open(self):
    if not self.port:
      return

    if self.ser:
      self.ser.close()
      self.ser = None

    try:
      self.ser = serial.Serial(self.port, 115200, timeout=self.timeout)
      if self.ser:
        # Apply currently loaded settings when the microcontroller connects.
        update_values()
        
    except queue.Full as e:
      logger.error('Could not set thresholds. Queue full.')
    except serial.SerialException as e:
      self.ser = None
      logger.exception('Error opening serial: %s', e)

  def Read(self):
    def ProcessValues(values):
      # Fix our sensor ordering.
      actual = []
      for i in range(num_sensors):
        actual.append(values[sensor_numbers[i]])
      broadcast(['values', {'values': actual}])
      time.sleep(0.01)

    def ProcessThresholds(values):
      cur_thresholds = self.profile_handler.GetCurrent("thresholds")
      # Fix our sensor ordering.
      actual = []
      for i in range(num_sensors):
        actual.append(values[sensor_numbers[i]])
      for i, (cur, act) in enumerate(zip(cur_thresholds, actual)):
        if cur != act:
          self.profile_handler.UpdateThresholds(i, act)

    while not thread_stop_event.is_set():
      if NO_SERIAL:
        offsets = [int(normalvariate(0, num_sensors+1)) for _ in range(num_sensors)]
        self.no_serial_values = [
          max(0, min(self.no_serial_values[i] + offsets[i], 1023))
          for i in range(num_sensors)
        ]
        broadcast(['values', {'values': self.no_serial_values}])
        time.sleep(0.01)
      else:
        if not self.ser:
          self.Open()
          # Still not open, retry loop.
          if not self.ser:
            time.sleep(1)
            continue

        try:
          # Send the command to fetch the values.
          self.write_queue.put('v\n', block=False)

          # Wait until we actually get the values.
          # This will block the thread until it gets a newline
          line = self.ser.readline().decode('ascii').strip()

          # All commands are of the form:
          #   cmd num1 num2 num3 num4
          parts = line.split()
          cmd = ''
          if len(parts) > 0: cmd = parts[0]

          if cmd == 'v':
            values = [int(x) for x in parts[1:]]
            ProcessValues(values)
          elif cmd == 't':
            values = [int(x) for x in parts[1:]]
            ProcessThresholds(values)
          else: print(line)
        except queue.Full as e:
          logger.error('Could not fetch new values. Queue full.')
        except serial.SerialException as e:
          logger.error('Error reading data: ', e)
          self.Open()

  def Write(self):
    while not thread_stop_event.is_set():
      try:
        command = self.write_queue.get(timeout=1)
      except queue.Empty:
        continue
      if NO_SERIAL:
        if command[0] == 't':
          self.profile_handler.Broadcast("thresholds")
          print('Thresholds are: ' +
            str(self.profile_handler.GetCurrent("thresholds")))
        else:
          sensor, threshold = int(command[0]), int(command[1:-1])
          for i, index in enumerate(sensor_numbers):
            if index == sensor:
              self.profile_handler.UpdateThresholds(i, threshold)
      else:
        if not self.ser:
          # Just wait until the reader opens the serial port.
          time.sleep(1)
          continue

        try:
          self.ser.write(command.encode() if type(command) is str else command)
        except serial.SerialException as e:
          logger.error('Error writing data: ', e)
          # Emit current thresholds since we couldn't update the values.
          self.profile_handler.Broadcast("thresholds")


profile_handler = ProfileHandler()
serial_handler = SerialHandler(profile_handler, port=SERIAL_PORT)

def update_threshold(values, index):
  try:
    # Let the writer thread handle updating thresholds.
    threshold_cmd = '%d %d\n' % (sensor_numbers[index], values[index])
    serial_handler.write_queue.put(threshold_cmd, block=False)
  except queue.Full:
    logger.error('Could not update thresholds. Queue full.')

def update_image(file):
  try:
    fullpath = os.path.join(images_dir, file)
    if (os.path.exists(fullpath)):
      with open('images/'+file, 'rb') as f:
        file_cmd = 'g %d\n' % (os.path.getsize(fullpath))
        serial_handler.write_queue.put(file_cmd, block=False)
        serial_handler.write_queue.put(f.read(), block=True)
      print("Updated image")
  except queue.Full:
    logger.error('Could not update image. Queue full.')

def add_profile(profile_name, data):
  profile_handler.AddProfile(profile_name, data)
  # When we add a profile, we are using the currently loaded thresholds so we
  # don't need to explicitly apply anything.


def remove_profile(profile_name):
  profile_handler.RemoveProfile(profile_name)
  update_values()


def change_profile(profile_name):
  profile_handler.ChangeProfile(profile_name)
  update_values()

def update_values():
  update_image(profile_handler.GetCurrent("image"))
  thresholds = profile_handler.GetCurrent("thresholds")
  for i in range(len(thresholds)):
    update_threshold(thresholds, i)



async def get_defaults(request):
  return json_response({
    'profiles': profile_handler.GetProfileNames(),
    'cur_profile': profile_handler.GetCurrentProfile(),
    'data': profile_handler.profiles[profile_handler.GetCurrentProfile()]
  })

out_queues = set()
out_queues_lock = threading.Lock()
main_thread_loop = asyncio.new_event_loop()


def broadcast(msg):
  with out_queues_lock:
    for q in out_queues:
      try:
        main_thread_loop.call_soon_threadsafe(q.put_nowait, msg)
      except asyncio.queues.QueueFull:
        pass


async def get_ws(request):
  ws = web.WebSocketResponse()
  await ws.prepare(request)

  request.app['websockets'].append(ws)
  print('Client connected')

  # Potentially fetch any threshold values from the microcontroller that
  # may be out of sync with our profiles.
  serial_handler.write_queue.put('t\n', block=False)

  queue = asyncio.Queue(maxsize=100)
  with out_queues_lock:
    out_queues.add(queue)

  profile_handler.BroadcastAll()

  try:
    queue_task = asyncio.create_task(queue.get())
    receive_task = asyncio.create_task(ws.receive())
    connected = True
    while connected:
      done, pending = await asyncio.wait([
        queue_task,
        receive_task,
      ], return_when=asyncio.FIRST_COMPLETED)

      for task in done:
        if task == queue_task:
          msg = await queue_task
          await ws.send_json(msg)

          queue_task = asyncio.create_task(queue.get())
        elif task == receive_task:
          msg = await receive_task

          if msg.type == WSMsgType.TEXT:
            data = msg.json()
            action = data[0]

            if action == 'update_threshold':
              values, index = data[1:]
              update_threshold(values, index)
            elif action == 'update_image':
              file = data[1]
              profile_handler.UpdateCurrentAndBroadcast("image", file)
              update_image(file)
            elif action == 'add_profile':
              profile_name, profile_data = data[1:]
              add_profile(profile_name, profile_data)
            elif action == 'remove_profile':
              profile_name, = data[1:]
              remove_profile(profile_name)
            elif action == 'change_profile':
              profile_name, = data[1:]
              change_profile(profile_name)
          elif msg.type == WSMsgType.CLOSE:
            connected = False
            continue

          receive_task = asyncio.create_task(ws.receive())
  except ConnectionResetError:
    pass
  finally:
    request.app['websockets'].remove(ws)
    with out_queues_lock:
      out_queues.remove(queue)

  queue_task.cancel()
  receive_task.cancel()

  print('Client disconnected')


build_dir = os.path.abspath(
  os.path.join(os.path.dirname(__file__), '..', 'build')
)
images_dir = os.path.abspath(
  os.path.join(os.path.dirname(__file__), 'images')
)

async def get_index(request):
  return web.FileResponse(os.path.join(build_dir, 'index.html'))

async def get_images(request):
  if "tail" in request.match_info:
    file = os.path.join(images_dir, request.match_info["tail"])
    if os.path.exists(file) and file.endswith('.gif'):
      return web.FileResponse(file)
  else: return json_response([f for f in os.listdir(images_dir)])

async def upload_images(request):
  reader = await request.multipart()

  field = await reader.next()
  data = await field.read()
  with Image(blob=bytes(data)) as img:
    if (img.format != 'gif'): 
      img.format = 'gif'
    img.coalesce()
    for x in range(0, len(img.sequence)):
      with img.sequence.index_context(x):
        img.background_color = Color('black')
    if (img.height != 64 or img.width / 64 not in [1, 2, 4]):
      img.transform(resize='64x64')
      for x in range(0, len(img.sequence)):
        with img.sequence.index_context(x):
          img.extent(width=64, height=64, gravity='center')
    for x in range(0, len(img.sequence)):
      with img.sequence.index_context(x):
        img.background_color = Color('black')
        img.alpha_channel = 'remove'
    if (len(img.sequence) > 8):
      seq_new = []
      seq_temp = []
      delays = [(x.index, x.delay) for x in img.sequence]
      duration = sum(x.delay for x in img.sequence)
      delays_acc = list(itertools.accumulate(x.delay for x in img.sequence))
      total = 8
      while len(seq_temp) < 8:
        seq_temp = []
        prev_idx = 0
        for x in range(0, total):
          if (prev_idx >= len(delays)): break
          idx = bisect.bisect_left(delays_acc, duration * (x + 1) / total)
          if (idx == prev_idx): idx = idx + 1
          seq_temp.append(max(delays[prev_idx:idx], key=lambda x: x[1])[0])
          prev_idx = idx
        if (len(seq_temp) <= 8): seq_new = seq_temp
        total = total + 1
      x = 0
      duration = 0
      while x < len(img.sequence):
        if (x not in seq_new):
          duration = duration + img.sequence[x].delay
          del img.sequence[x]
          seq_new = [x - 1 for x in seq_new]
        else:
          if(duration > 0):
            img.sequence[x - 1].delay = img.sequence[x - 1].delay + duration
            duration = 0
          x = x + 1
      if(duration > 0):
        img.sequence[-1].delay = img.sequence[-1].delay + duration
    fname = field.filename
    fname = fname[:fname.rindex('.')] + '.gif'
    img.save(filename=os.path.join(images_dir, fname))
    return web.Response(text='{} sized of {} successfully stored'
                          ''.format(fname, img.size))

async def get_local_profiles(request):
  profiles = {}
  for profile in os.listdir(LOCAL_PROFILES_PATH):
    with open(os.path.join(LOCAL_PROFILES_PATH, profile, 'Editable.ini')) as f:
      name = next(filter(lambda l: l.startswith('DisplayName='), f.readlines()))
      name = name.removeprefix('DisplayName=').removesuffix('\n')
      profiles[name] = profile
  return json_response(profiles)

async def get_local_profile(request):
  basepath = os.path.join(LOCAL_PROFILES_PATH, request.match_info['profile'], 'Screenshots', 'Simply_Love')
  screenshots = {}
  for path in os.walk(basepath):
    if (len(path[2]) > 0): screenshots[path[0].replace(basepath, '')] = [p for p in path[2]]
  return json_response(screenshots)

async def download_screenshot(request):
  return web.FileResponse(os.path.join(LOCAL_PROFILES_PATH, request.match_info['profile'], 'Screenshots', 'Simply_Love', request.match_info['ss']))

async def on_startup(app):
  profile_handler.MaybeLoad()

  read_thread = threading.Thread(target=serial_handler.Read)
  read_thread.start()

  write_thread = threading.Thread(target=serial_handler.Write)
  write_thread.start()

async def on_shutdown(app):
  for ws in app['websockets']:
    await ws.close(code=WSCloseCode.GOING_AWAY, message='Server shutdown')
  thread_stop_event.set()

app = web.Application()

# List of open websockets, to close when the app shuts down.
app['websockets'] = []

app.add_routes([
  web.get('/defaults', get_defaults),
  web.get('/ws', get_ws),
])
if not NO_SERIAL:
  app.add_routes([
    web.get('/', get_index),
    web.get('/plot', get_index),
    web.get('/image-select', get_index),
    web.get('/browse-screenshots', get_index),
    web.get('/images', get_images),
    web.get('/images/{tail:.*}', get_images),
    web.get('/local-profiles', get_local_profiles),
    web.get('/local-profiles/{profile:\d{8}}', get_local_profile),
    web.get('/screenshots/{profile:\d{8}}/{ss:.*}', download_screenshot),
    web.post('/images/upload', upload_images),
    web.static('/', build_dir),
  ])
app.on_shutdown.append(on_shutdown)
app.on_startup.append(on_startup)

if __name__ == '__main__':
  hostname = socket.gethostname()
  ip_address = socket.gethostbyname(hostname)
  print(' * WebUI can be found at: http://' + ip_address + ':' + str(HTTP_PORT))

  main_thread_loop.run_until_complete(web._run_app(app, port=HTTP_PORT))