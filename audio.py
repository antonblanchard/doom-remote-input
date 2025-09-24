#!/usr/bin/python3

import threading
import queue
import time
import os

# Import GStreamer libraries
import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst, GLib

class AudioPlayerPool:
    """
    A pool of worker threads to play audio files concurrently via GStreamer.
    Each worker grabs an id from a queue and plays the associated audio from
    the audio_files dictionary, allowing sounds to overlay.
    """
    def __init__(self, audio_files, num_workers=4, verbose=False):
        # Initialize GStreamer
        Gst.init(None)

        self.num_workers = num_workers
        self.task_queue = queue.Queue()
        self.audio_files = audio_files
        self.verbose = verbose
        self.workers = []

        print(f"Starting audio pool with {self.num_workers} worker threads...")
        self._start_workers()

    def _start_workers(self):
        """Creates and starts the worker threads."""
        for i in range(self.num_workers):
            # The 'daemon=True' ensures threads exit when the main program does
            thread = threading.Thread(target=self._worker_loop, args=(i+1,), daemon=True)
            thread.start()
            self.workers.append(thread)

    def _worker_loop(self, worker_id):
        """The main loop for each worker thread."""
        if self.verbose:
            print(f"Worker {worker_id}: Ready for tasks.")
        while True:
            # Block and wait for a sound id from the queue
            id = self.task_queue.get()

            # A 'None' sentinel value is used to signal the thread to exit
            if id is None:
                print(f"Worker {worker_id}: Received shutdown signal. Exiting.")
                break

            if self.verbose:
                print(f"Worker {worker_id}: Playing sound {id}")

            loop = GLib.MainLoop()

            pipeline_str = (
                "appsrc name=audio_source ! "
                "audio/x-raw, format=U8, rate=11025, channels=1, layout=interleaved ! "
                "audioconvert ! audioresample ! autoaudiosink"
            )
            pipeline = Gst.parse_launch(pipeline_str)

            appsrc = pipeline.get_by_name("audio_source")

            # Convert your python bytes into a GStreamer Buffer
            gst_buffer = Gst.Buffer.new_wrapped(bytes(self.audio_files[id]))

            # Push the buffer into the pipeline
            appsrc.emit("push-buffer", gst_buffer)

            # Signal that you have no more data to push
            appsrc.emit("end-of-stream")

            # This function will be called when a message is posted on the bus.
            def on_message(bus, message):
                t = message.type
                if t == Gst.MessageType.EOS:
                    if self.verbose:
                        print("End-of-Stream reached.")
                    loop.quit()
                elif t == Gst.MessageType.ERROR:
                    err, debug = message.parse_error()
                    print(f"Error: {err}, {debug}")
                    loop.quit()
                return True

            # Add a message handler to the pipeline's bus
            bus = pipeline.get_bus()
            bus.add_signal_watch()
            bus.connect("message", on_message)

            # Start playing
            if self.verbose:
                print("Starting pipeline...")
            pipeline.set_state(Gst.State.PLAYING)

            try:
                loop.run()
            finally:
                # Clean up
                if self.verbose:
                    print("Stopping pipeline...")
                pipeline.set_state(Gst.State.NULL)


    def play_sound(self, id):
        if self.verbose:
            print(f"Main: Queuing sound -> {id}")
        self.task_queue.put(id)


    def stop(self):
        """Stops all worker threads gracefully."""
        print("Main: Sending shutdown signal to all workers...")
        # Put one 'None' for each worker to unblock them
        for _ in self.workers:
            self.task_queue.put(None)
        
        # Wait for all tasks in the queue to be processed
        self.task_queue.join()
        print("Main: Audio pool has been shut down.")

