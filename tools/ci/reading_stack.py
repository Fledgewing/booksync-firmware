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
env.AddBuildMiddleware(bounded_reading_frame, "*/SeriesPlan.cpp")

env.AddBuildMiddleware(bounded_reading_frame, "*/SeriesQueue.cpp")
env.AddBuildMiddleware(bounded_reading_frame, "*/SeriesQueueStore.cpp")

env.AddBuildMiddleware(bounded_reading_frame, "*/SeriesFlow.cpp")
env.AddBuildMiddleware(bounded_reading_frame, "*/Series.cpp")
# F40w projections share the same small-frame requirement as their F39 source.
env.AddBuildMiddleware(bounded_reading_frame, "*/widgets/StatsWorker.cpp")
env.AddBuildMiddleware(bounded_reading_frame, "*/services/widgets/Week.cpp")
env.AddBuildMiddleware(bounded_reading_frame, "*/services/widgets/Streak.cpp")

env.AddBuildMiddleware(bounded_reading_frame, "*/services/widgets/Year.cpp")
env.AddBuildMiddleware(bounded_reading_frame, "*/services/widgets/Monthly.cpp")

env.AddBuildMiddleware(bounded_reading_frame, "*/services/widgets/Today.cpp")
env.AddBuildMiddleware(bounded_reading_frame, "*/services/widgets/Pages.cpp")


def bounded_native_application_frame(node_env, node):
    # Borrowed metadata and one small codec header/publication control block;
    # raw captures/reviews and digest buffers remain in reusable worker storage.
    return node_env.Object(node, CCFLAGS=node_env["CCFLAGS"] + [
        "-fstack-usage", "-Wstack-usage=512", "-Werror=stack-usage=512",
    ])


for source in ("SourceFile.cpp", "ProgressCapture.cpp", "NativeCaptureGate.cpp",
               "NativeProgress.cpp", "NativeStateStore.cpp", "NativeState.cpp"):
    env.AddBuildMiddleware(bounded_native_application_frame, "*/" + source)
env.AddBuildMiddleware(bounded_reminder_frame, "*/ReminderPresenter.cpp")
env.AddBuildMiddleware(bounded_reminder_frame, "*/DueSheet.cpp")
env.AddBuildMiddleware(bounded_reminder_frame, "*/RemindersFormat.cpp")
env.AddBuildMiddleware(bounded_reading_frame, "*/services/widgets/When.cpp")
env.AddBuildMiddleware(bounded_reading_frame, "*/services/widgets/Records.cpp")

env.AddBuildMiddleware(bounded_reading_frame, "*/services/widgets/Goal.cpp")
env.AddBuildMiddleware(bounded_reading_frame, "*/services/widgets/Forecast.cpp")
env.AddBuildMiddleware(bounded_reminder_frame, "*/ReminderSettingsScreen.cpp")
env.AddBuildMiddleware(bounded_reminder_frame, "*/ui/reminders/Settings.cpp")

env.AddBuildMiddleware(bounded_reminder_frame, "*/ReminderText.cpp")
env.AddBuildMiddleware(bounded_reading_frame, "*/ChapterCapture.cpp")
