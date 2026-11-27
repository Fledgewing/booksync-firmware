"""Never compile the archived native sync transport, even via library discovery."""
Import("env")

def omit_native_writer(node_env, node):
    return None

env.AddBuildMiddleware(omit_native_writer, "*/KOReaderSyncClient.cpp")
