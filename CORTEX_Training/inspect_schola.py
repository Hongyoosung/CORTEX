try:
    from schola.gym.env import GymEnv
    import inspect
    print("GymEnv found.")
    print(f"Signature: {inspect.signature(GymEnv.__init__)}")
    print(f"Docstring: {GymEnv.__init__.__doc__}")
except ImportError:
    print("Schola not installed.")
except Exception as e:
    print(f"Error: {e}")
