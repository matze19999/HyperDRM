# HyperDRM 0.15.7

Send frames as soon as they are ready when the receiver connection is free. Poll for confirmations more frequently during capture and preserve the configured sampling cadence through small scheduling delays. JSON and FlatBuffer retain bounded queues and coalesce old frames if the receiver falls behind. Histogram preset settling remains time-based and the default capture interval stays at 50 ms.
