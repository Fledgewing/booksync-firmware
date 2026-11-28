"""Compile F39's fixed workspaces without large compiler-generated stack frames."""
Import("env")  # noqa: F821 (PlatformIO/SCons)


def bounded_reading_frame(node_env, node):
    # Fixed arrays belong to caller-owned workspaces. In particular an aggregate
    # reset must never silently materialize a 6 KiB temporary on a 4 KiB task.
    return node_env.Object(node, CCFLAGS=node_env["CCFLAGS"] + [
        "-fstack-usage", "-Wstack-usage=256", "-Werror=stack-usage=256",
    ])


env.AddBuildMiddleware(bounded_reading_frame, "*/PersonalReading.cpp")
env.AddBuildMiddleware(bounded_reading_frame, "*/ReadingStats.cpp")

# F20 keeps payloads in borrowed worker storage; retain exact hardware frame
# measurements separately from F39's stricter pure-projection gate.
def finish_frames(node_env, node):
    return node_env.Object(node, CCFLAGS=node_env["CCFLAGS"] + ["-fstack-usage"])

env.AddBuildMiddleware(finish_frames, "*/Finish.cpp")
env.AddBuildMiddleware(finish_frames, "*/SyncOwner.cpp")

def bounded_reminder_frame(node_env, node):
    # R1 permits one rollback/codec record, never a list/schedule temporary.
    return node_env.Object(node, CCFLAGS=node_env["CCFLAGS"] + [
        "-fstack-usage", "-Wstack-usage=1024", "-Werror=stack-usage=1024",
    ])


env.AddBuildMiddleware(bounded_reminder_frame, "*/Reminders.cpp")
env.AddBuildMiddleware(bounded_reminder_frame, "*/RemindersFacade.cpp")
env.AddBuildMiddleware(bounded_reminder_frame, "*/RemindersStore.cpp")
env.AddBuildMiddleware(bounded_reminder_frame, "*/RemindersRuntime.cpp")
