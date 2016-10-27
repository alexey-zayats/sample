<?php

Class Dispatcher {
	private static $instance = 0;
	private $path;

	private function __clone() {}
	private function __construct() {
		$path = _CONTROLL_ . _DS_;
		if (is_dir($path) == false) {
			throw new Exception ('Controllers directory does not exists: `' . $path . '`');
		}
		$this->path = $path;
	}

	public static function getInstance() {
		if ( self::$instance == 0 )
			self::$instance = new Dispatcher();
		return self::$instance;
	}

	public function delegate() {
		$argc = 0;
		$argv = array();

		// Analyze route
		$this->getController($file, $controller, $action, $argc, $argv );

		// File available?
		if (is_readable($file) == false) {
			$this->notFound('no-file');
		}

		// Include the file
		include ($file);

		// Initiate the class
		$class = ucfirst($controller).'Controller';
		$controller = new $class();

		// Action available?
		if (is_callable(array($controller, $action)) == false) {
			$action = 'index';
		}
		echo $controller->$action($argc,$argv);
	}

	private function getController(&$file, &$controller, &$action, &$argc, &$argv) {
		$route = (empty($_SERVER['PATH_INFO'])) ? '' : $_SERVER['PATH_INFO'];
		if (empty($route)) { $route = 'index'; }
		// Get separate parts
		$route = trim($route, '/\\');
		$argv = explode('/', $route);

		// Find right controller
		$cmd_path = $this->path;
		foreach ($argv as $part) {
			$fullpath = $cmd_path . $part;

			// Is there a dir with this path?
			if (is_dir($fullpath)) {
				$cmd_path .= $part . _DS_;
				array_shift($argv);
				continue;
			}

			// Find the file
			if (is_file($fullpath . '.inc')) {
				$controller = $part;
				array_shift($argv);
				break;
			}
		}

		if (empty($controller)) { $controller = 'index'; };

		$argc = count($argv);


		// Get action
		$action = $argc > 0 ? $argv[0] : '';
		if (empty($action)) { $action = 'index'; }

		$file = $cmd_path . $controller . '.inc';
	}


	private function notFound($reason) {
		$t = new Template('404.html');
		$t->set('/reason', $reason );
		echo $t;
	}

}

?>
