try:
    from schola.core.unreal_connections.standalone_connection import StandaloneUnrealConnection
    from schola.core.unreal_connections.editor_connection import UnrealEditorConnection
    import inspect
    
    print("StandaloneUnrealConnection found.")
    print(f"Signature: {inspect.signature(StandaloneUnrealConnection.__init__)}")
    
    print("UnrealEditorConnection found.")
    print(f"Signature: {inspect.signature(UnrealEditorConnection.__init__)}")

except Exception as e:
    print(f"Error: {e}")
