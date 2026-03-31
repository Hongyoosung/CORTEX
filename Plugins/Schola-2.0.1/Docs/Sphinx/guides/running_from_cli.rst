Running with a Standalone Environment from CLI
==============================================

Running Schola from the command line interface (CLI) is a powerful way to interact with the system. This guide will walk you through the steps to run Schola from the CLI, including how to set up your environment and execute commands.


Building Your Environment
-------------------------

Before you can run a standalone executable from the Command-line with Schola, you need to build your environment into a standalone executable. This involves packaging your Unreal Engine project which is detailed in the  `official unreal engine documentation <https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-unreal-engine-projects>`_.

Running From CLI
----------------

To launch a standalone environment (i.e. A game built in Development or Shipping mode), you can use the following command:

.. tabs::

    .. group-tab:: Stable Baselines 3
        .. code-block:: bash
            
            schola sb3 train [ppo|sac] --simulator-type executable --executable-path <PATH_TO_EXECUTABLE>

    .. group-tab:: RLlib
        .. code-block:: bash
            
            schola rllib train [ppo|sac|impala] --simulator-type executable --executable-path <PATH_TO_EXECUTABLE>

The `<PATH_TO_EXECUTABLE>` should be replaced with the path to your packaged Unreal Engine executable.


Headless Mode
~~~~~~~~~~~~~

Schola can be run in headless mode, which is useful for running scripts or automating tasks. To run Schola in headless mode, use the following command:

.. tabs::
    
    .. group-tab:: Stable Baselines 3
        .. code-block:: bash
            
            schola sb3 train [ppo|sac] --simulator-type executable --executable-path <PATH_TO_EXECUTABLE> --headless

    .. group-tab:: RLlib
        .. code-block:: bash
            
           schola rllib train [ppo|sac|impala] --simulator-type executable --executable-path <PATH_TO_EXECUTABLE> --headless


This command will start Schola without the graphical user interface (GUI), allowing for accelerated simulation speeds.

.. note::
    Any features requiring rendering will not work when running in headless mode (e.g. :cpp:class:`CameraObserver <UCameraObserver>`).

Fixed Simulation Timestep
~~~~~~~~~~~~~~~~~~~~~~~~~

Schola allows you to set a fixed frames per second (FPS) for the simulation. This can be useful for ensuring consistent performance across different runs. To set a fixed FPS, use the following command: 

.. tabs::

    .. group-tab:: Stable Baselines 3
        .. code-block:: bash
            
            schola sb3 train [ppo|sac] --simulator-type executable --executable-path <PATH_TO_EXECUTABLE> --fps <FPS>

    .. group-tab:: RLlib
        .. code-block:: bash
            
            schola rllib train [ppo|sac|impala] --simulator-type executable --executable-path <PATH_TO_EXECUTABLE> --fps <FPS>

Replace `<FPS>` with the desired frames per second value. For example, to set the FPS to 30, use:

.. tabs::
    
    .. group-tab:: Stable Baselines 3
        .. code-block:: bash
             
             schola sb3 train [ppo|sac] --simulator-type executable --executable-path <PATH_TO_EXECUTABLE> --fps 30

    .. group-tab:: RLlib
        .. code-block:: bash
           
           schola rllib train [ppo|sac|impala] --simulator-type executable --executable-path <PATH_TO_EXECUTABLE> --fps 30

.. note::
    The FPS determines the delta used when calculating updates in Unreal Engine, however the number of timesteps simulated per second is independant of this setting. For example if `--fps=100` and Unreal simulates your environment at 1000fps then for every second in the real world, 10 seconds in the environment will be simulated. 

Controlling The Map
~~~~~~~~~~~~~~~~~~~

Schola allows you to specify the map to load when launching the environment. To do this, use the `--map` argument followed by the path to the map. For example:

.. tabs::
    
    .. group-tab:: Stable Baselines 3
        .. code-block:: bash
            
            schola sb3 train [ppo|sac] --simulator-type executable --executable-path <PATH_TO_EXECUTABLE> --map <MAP_NAME>

    .. group-tab:: Ray
        .. code-block:: bash
            
            schola rllib train [ppo|sac|impala] --simulator-type executable --executable-path <PATH_TO_EXECUTABLE> --map <MAP_NAME>

The map should be specified as a relative path from the `Content` folder, with content replaced with `Game` For exammple `/Content/LevelOne/Map` would be specified as `Game/LevelOne/Map`.

.. note::
    The map must be a valid Unreal Engine map file. If the map is not found or isn't specified, Schola will default to the main map specified in the project settings.

.. note::
    The map parameter will not work with Shipping builds by default, you need to take additional steps to allow the map to be loaded based on a command line flag. 
