import sys
import time
import logging
from os import path
from selenium import webdriver
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support.expected_conditions import presence_of_element_located
from selenium.webdriver.firefox.service import Service

start_time = time.time_ns() / 1_000_000 # Start time in milliseconds
print(start_time)

url = '10.0.0.100:33333'
client_name = 'client'
sleep_time = 0.0
threaded = True
headless = True

print('Starting browser')

print('Arguments:' + str(sys.argv))

for index, arg in enumerate(sys.argv[1:], start=1):
    if index == 1:
        url = arg
    elif index == 2:
        client_name = arg
    elif index == 3:
        sleep_time = float(arg)
    elif index == 4:
        threaded = arg.lower() == 'true'
    elif index == 5:
        headless = arg.lower() == 'true'

print(f'URL: {url}')
print(f'Client name: {client_name}')
print(f'Sleep time: {sleep_time}')
print(f'Headless: {headless}')

logger = logging.getLogger('client')
logger.setLevel(logging.DEBUG)
file_handler = logging.FileHandler(f'{client_name}.metric.log')
formatter = logging.Formatter('%(message)s')
formatter_with_time = logging.Formatter('%(asctime)s;client_running;%(message)s')
file_handler.setFormatter(formatter_with_time)
logger.addHandler(file_handler)
logger.info("0")

class BasicMetricsStore:
    def __init__(self):
        self.metrics = {}

    def input(self, text):
        # The input is a string with the format "key;value" or "timestamp;key;value"
        # We just want to store the value for each key
        parts = text.split(';')
        if len(parts) == 2:
            self.metrics[parts[0]] = parts[1]
        elif len(parts) == 3:
            self.metrics[parts[1]] = parts[2]
        else:
            return

        self.save(client_name + '.metrics')

    def save(self, filename):
        output = ""
        for key, value in self.metrics.items():
            output += f"{key};{value}\n"
        with open(filename, 'w') as file:
            file.write(output)

try:

    options = webdriver.FirefoxOptions()
    options.binary = '/usr/local/bin/firefox'
    if headless:
        options.add_argument("-headless")
    service = Service(log_output=path.devnull)
    service.path='/usr/local/bin/geckodriver'
    with webdriver.Firefox(options=options, service=service) as driver:

        metrics_store = BasicMetricsStore()

        ms_since_start = round((time.time_ns() / 1_000_000) - start_time) # in milliseconds
        time_to_sleep = (sleep_time * 1000) - ms_since_start # in milliseconds (sleep_time is in seconds)

        if time_to_sleep > 0:
            print(f"Sleeping for {time_to_sleep} milliseconds to get to {sleep_time} seconds")
            time.sleep(time_to_sleep / 1000)
        else:
            print(f"Sleep time is negative: {time_to_sleep} milliseconds!")

        # Log that we have started
        logger.info("1")
        file_handler.setFormatter(formatter)

        # Load page
        print(f'Opening: {url}')
        driver.get(url)

        ms_since_start = round((time.time_ns() / 1_000_000) - start_time) # in milliseconds
        print(f'Page loaded after {ms_since_start} milliseconds since start, including {sleep_time} seconds of sleep time')

        print('Waiting for video element to be loaded')
        wait = WebDriverWait(driver, 10)
        wait.until(presence_of_element_located((By.ID, 'video')))

        print('Waiting for video stream to be loaded')
        while not driver.execute_script("return isLoaded();"):
            buffer = driver.execute_script("return readLog();")
            if len(buffer) == 0:
                time.sleep(0.01)
            if buffer == 'STOP':
                print('Video stream failed to load')
                break
            for line in buffer.splitlines():
                if line.startswith('#'):
                    print(line) # We write comments to the console.
                else:
                    logger.info(line)
                    metrics_store.input(line)
            

        print('Get the video element and start playback if paused')
        video_element = driver.find_element(By.ID, 'video')
        is_paused = driver.execute_script("return isPaused();")
        if is_paused:
            video_element.click()

        ms_since_start = round((time.time_ns() / 1_000_000) - start_time) # in milliseconds
        print(f'Playback started at {ms_since_start} milliseconds since start, including {sleep_time} seconds of sleep time')


        print('Print logs')
        # Geckodriver does not support getting console logs so we retrieve the logs from a buffer in js.
        buffer = driver.execute_script("return readLog();")
        while buffer != 'STOP':
            if len(buffer) == 0:
                time.sleep(0.2)
                # Check if the video is still loaded
                '''
                if driver.execute_script("return isLoaded();"):
                    # Force the video to play if it is paused
                    if driver.execute_script("return isPaused();"):
                        print('Video is paused, forcing play')
                        video_element.click()
                '''
            else:
                for line in buffer.splitlines():
                    if line.startswith('#'):
                        print(line) # We write comments to the console.
                    else:
                        logger.info(line)
                        metrics_store.input(line)


            buffer = driver.execute_script("return readLog();")

except Exception as e:
    print('# Error')
    print(e)

file_handler.setFormatter(formatter_with_time)
logger.info("0")
print('# Done')
