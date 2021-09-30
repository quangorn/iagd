﻿using System;
using System.Collections.Concurrent;
using System.Runtime.ExceptionServices;
using System.Threading;
using EvilsoftCommons.Exceptions;
using log4net;

namespace IAGrim.Database.Synchronizer.Core {
    /// <summary>
    /// The thread executor ensures that all calls to SQLite runs on the same thread.
    /// SQLite has no support for access across multiple threads.
    /// </summary>
    public class ThreadExecuter : IDisposable {
        private static readonly ILog Logger = LogManager.GetLogger(typeof(ThreadExecuter));
        private readonly ConcurrentDictionary<AutoResetEvent, object> _results = new ConcurrentDictionary<AutoResetEvent, object>();
        private readonly ConcurrentQueue<QueuedExecution> _queue = new ConcurrentQueue<QueuedExecution>();
        private Thread _thread;
        private volatile bool _isCancelled;
#if DEBUG
        public const int ThreadTimeout = 1000 * 60 * 20;
#else
        public const int ThreadTimeout = 1000 * 60 * 30;
#endif

        public ThreadExecuter() {
            _isCancelled = false;
            _thread = new Thread(new ThreadStart(Run));
            _thread.Start();

            while (!_thread.IsAlive) ;
        }

        private void Run() {
            if (Thread.CurrentThread.Name == null) {
                Thread.CurrentThread.Name = "SQL";
                Thread.CurrentThread.CurrentUICulture = new System.Globalization.CultureInfo("en-US");
            }

            ExceptionReporter.EnableLogUnhandledOnThread();

            while (!_isCancelled) {
                if (_queue.TryDequeue(out var elem)) {
                    try {
                        elem.IsStarted = true;

                        if (elem.Func != null)
                            _results[elem.Trigger] = elem.Func();
                        else
                            elem.Action();
                    }
                    catch (Exception ex) {
                        _results[elem.Trigger] = ex;
                    }

                    elem.Trigger.Set();
                }

                try {
                    Thread.Sleep(1);
                }
                catch (Exception) {
                }
            }
        }


        public void Execute(Action func, int timeout = ThreadTimeout) {
            if (_thread == null)
                throw new InvalidOperationException("Object has been disposed");
            AutoResetEvent ev = new AutoResetEvent(false);

            var item = new QueuedExecution {
                Action = () => func(),
                Trigger = ev
            };
            _queue.Enqueue(item);

            if (!ev.WaitOne(timeout, true)) {
                throw new Exception($"Operation never terminated: Started: {item.IsStarted}");
            }

            if (_results.ContainsKey(ev)) {
                object val;
                if (_results.TryRemove(ev, out val)) {
                    var ex = val as Exception;
                    Logger.Warn(ex.Message);
                    Logger.Warn(ex.StackTrace);
                    ExceptionDispatchInfo.Capture(ex).Throw();
                }
            }
        }

        public T Execute<T>(Func<T> func) {
            return Execute(func, ThreadTimeout);
        }

        private T Execute<T>(Func<T> func, int timeout) {
            if (_thread == null)
                throw new InvalidOperationException("Object has been disposed");
            AutoResetEvent ev = new AutoResetEvent(false);

            var item = new QueuedExecution {
                Func = () => func(),
                Trigger = ev
            };
            _queue.Enqueue(item);

            if (!ev.WaitOne(timeout, true)) {
                throw new Exception($"Operation never terminated: Started: {item.IsStarted}");
            }

            
            object result;

            if (_results.TryRemove(ev, out result)) {
                Exception ex = result as Exception;
                if (ex != null) {
                    Logger.Warn(ex.Message);
                    Logger.Warn(ex.StackTrace);
                    ExceptionDispatchInfo.Capture(ex.InnerException).Throw();
                }
            }

            return (T) result;
        }

        ~ThreadExecuter() {
            Dispose();
        }

        public void Dispose() {
            _isCancelled = true;
            _thread = null;
        }


        class QueuedExecution {
            public Func<object> Func { get; set; }
            public Action Action { get; set; }
            public AutoResetEvent Trigger { get; set; }

            /// <summary>
            /// Helps track down which operation stalled.
            /// Multiple operations can be queued and time out, but only one of them will have started.
            /// </summary>
            public volatile bool IsStarted = false;
        }
    }
}