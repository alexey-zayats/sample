<?php

include_once 'dbi.inc';
include_once 'object.inc';
include_once 'user.inc';

class AuthError Extends Exception {
	public function __counstruct($message, $code = 0) {
		paretn::__construct($message,$code);
	}
	public function __toString() {
		return __CLASS__ . ": [{$this->code}]: {$this->message}\n";
	}
}

class sql
{
	const equal     = 1;          // just equal: id = 10
	const not_equal = 2;          // just not equal: id <> 10
	const greater   = 4;          // used with Equal: id >= 10
	const less      = 8;          // used with Equal: id <= 10
	const interval  = 16;         // search in range: ( 1 < id ) and ( id < 10 )
	const segment   = 32;         // search in range with end-points: ( 1 <= id ) and ( id <= 10 )
	const inset     = 64;         // find in set: id IN ( 1,2,3 )
	const like      = 128;        // pattern matching
	const ilike     = 256;        // case insensitive tree as ILIKE or lower()
	const field     = 512;
	const _and_     = 1024;
	const _or_      = 2048;
	const match			= 4096;
};

class MCache {
	private static $cache = null;
	private $data = null;
	private $opened = false;

	public function __construct() {
		$this->cache = new Memcache;
		$data = array();
	}

	public function connect( $host = 'localhost', $port = '11211' ) {
		$this->opened = ( $this->cache ) ? $this->cache->connect( $host, $port ) : false;
	}

	public function set( $k, $v ) {
		if ( $this->opened ) $this->cache->set( $k, $v, 0, 600 );
		$this->data[ $k ] = $v;
		return ;
	}

	public function get( $k ) {
		if ( !isset( $this->data[$k] ) && $this->opened ) $this->data[ $k ] = $this->cache->get( $k );
		return ( isset($this->data[ $k ]) ) ? $this->data[ $k ] : false;
	}
	
}


class Keeper {
	public static $instance = null;
	private $user = null;
	private $dbh = null;
	private $wheres = null;
	private $binds = null;
	public static $cache = null;

	// prevent to clone singleton instance
	private function __clone() { die('Could not clone Keeper instance. It is singleton.'); }

	private function __construct() {
		session_start();
		$this->cache = new MCache;
	}

	public static function getInstance() {
		if ( self::$instance == null )	self::$instance = new self();
		return self::$instance;
	}

	public function connect($server, $dbname, $username, $password = null) {
		if ( self::$instance == null ) die('It is singleton. use Keeper::getInstance()->connect()');
		if ( $this->dbh ) $this->dbh->disconnect();
		$this->dbh = new DBI($server, $dbname, $username, $password);
	}

	// to use in plain queries that couldn't use Keeper approach
	public function dbh() {
		return $this->dbh;
	}

	// check authenticator
	// Keeper::getInstance()->authenticate();
	// if user is not authed, we will see an authentication form
	public function authenticate() {

		$username = null;
		$password = null;

		if ( $_SERVER['REQUEST_METHOD'] === 'POST' && ( isset($_POST['username']) && isset($_POST['password']) ) ) {
			$username = $_POST['username'];
			$password = $_POST['password'];
		} else if ( isset($_SESSION['authhash']) ) {
			list($username, $password) = explode( ':', base64_decode($_SESSION['authhash']) );
		}

		if ($username && $password) {
			$user = new User( array('name' => array( sql::equal, $username ) ) );
			if (!$user->id) {
				throw new AuthError('No such user', -1 );
			} else if ( $user->password !== $password ) {
				throw new AuthError('Password missmatch', -2 );
			} else if ( !$user->enabled ) {
				throw new AuthError('Account locked', -3);
			}

			$_SESSION['authhash'] = base64_encode( sprintf("%s:%s", $user->name, $user->password ) );
			return true;
		}

		$t = new Template('authenticate.html');
		$t->set('/debug', _DEBUG_);
		echo $t->parse();
		exit();
	}

	public function object_by_type( $o_type, $param = null ) {
		$o = null;
		switch ( $o_type ) {
			case Type::Group: $o = new Group($param); break;
			case Type::User: $o = new User($param); break;
			case Type::Topic: $o = new Topic($param); break;
			case Type::Article: $o = new Article($param); break;
			case Type::File: $o = new File($param); break;
			case Type::Tie: $o = new Tie($param); break;
			case Type::Param: $o = new Param($param); break;
			case Type::Flats: $o = new Flats($param); break;
			case Type::FlatsLease: $o = new FlatsLease($param); break;
			case Type::Houses: $o = new Houses($param); break;
			case Type::HousesLease: $o = new HousesLease($param); break;
			case Type::NewHousesCity: $o = new NewHousesCity($param); break;
			case Type::NewHousesHutment: $o = new NewHousesHutment($param); break;
			case Type::Genplan: $o = new Genplan($param); break;
			case Type::Driftage: $o = new Driftage($param); break;
			case Type::Privat: $o = new Privat($param); break;
			case Type::Jurist: $o = new Jurist($param); break;
			case Type::EIRC: $o = new EIRC($param); break;
			case Type::Companies: $o = new Companies($param); break;
			case Type::Exhibitions: $o = new Exhibitions($param); break;
		}
		return $o;
	}

	public function fields_by_type( $o_type ) {
		$o = null;
		switch ( $o_type ) {
			case Type::Group: $o = new Group(); break;
			case Type::User: $o = new User(); break;
			case Type::Topic: $o = new Topic(); break;
			case Type::Article: $o = new Article(); break;
			case Type::File: $o = new File(); break;
			case Type::Tie: $o = new Tie(); break;
			case Type::Param: $o = new Param(); break;
			case Type::Flats: $o = new Flats(); break;
			case Type::FlatsLease: $o = new FlatsLease(); break;
			case Type::Houses: $o = new Houses(); break;
			case Type::HousesLease: $o = new HousesLease(); break;
			case Type::NewHousesCity: $o = new NewHousesCity(); break;
			case Type::NewHousesHutment: $o = new NewHousesHutment(); break;
			case Type::Genplan: $o = new Genplan(); break;
			case Type::Driftage: $o = new Driftage(); break;
			case Type::Privat: $o = new Privat(); break;
			case Type::Jurist: $o = new Jurist(); break;
			case Type::EIRC: $o = new EIRC(); break;
			case Type::Companies: $o = new Companies(); break;
			case Type::Exhibitions: $o = new Exhibitions(); break;
		}
		return $o->schema();
	}

	private function table_by_type( $o_type ) {
		$o = null;
		switch ( $o_type ) {
			case Type::Group: $o = new Group(); break;
			case Type::User: $o = new User(); break;
			case Type::Topic: $o = new Topic(); break;
			case Type::Article: $o = new Article(); break;
			case Type::File: $o = new File(); break;
			case Type::Tie: $o = new Tie(); break;
			case Type::Param: $o = new Param(); break;
			case Type::Flats: $o = new Flats(); break;
			case Type::FlatsLease: $o = new FlatsLease(); break;
			case Type::Houses: $o = new Houses(); break;
			case Type::HousesLease: $o = new HousesLease(); break;
			case Type::NewHousesCity: $o = new NewHousesCity(); break;
			case Type::NewHousesHutment: $o = new NewHousesHutment(); break;
			case Type::Genplan: $o = new Genplan(); break;
			case Type::Driftage: $o = new Driftage(); break;
			case Type::Privat: $o = new Privat(); break;
			case Type::Jurist: $o = new Jurist(); break;
			case Type::EIRC: $o = new EIRC(); break;
			case Type::Companies: $o = new Companies(); break;
			case Type::Exhibitions: $o = new Exhibitions(); break;
		}
		return $o->table();
	}

	public function parse_query_map( $o_type, $table, $fields, $wlist, $param ) {
		$sql = null;
		$names = $wlist;
		$this->binds = array();
		$this->wheres = array();
		$orders = null; // string
		$groups = null; // string
		$limit = null; 	// int
		$offset = null; // int

		if ( array_key_exists( 'order', $param ) && is_array( $param['order'] ) ) {
			/* expectation:
				'order' => array(
					'id' => 'asc',
					'ctime'=> 'desc'
				)
			*/
			$v = array();
			$p = $param['order'];
			foreach ( $p as $k => $val ) {
				$v[] = sprintf( '%s %s', $k, $val );
			}
			$orders = ' ORDER BY '.implode( ', ', $v );
		}

		if ( array_key_exists( 'group', $param ) && is_array( $param['group'] ) ) {
			/*
				expectation:
				'group' => array( 'id', 'ctime' )
			*/
			$v = $param['group'];
			$groups = ' GROUP BY '.implode(', ', $v);
		}

		if ( array_key_exists( 'match', $param ) && is_array( $param['match'] ) ) {
			/*
				expectation:
					'match' => array( 'search expression', use_boolean_mode = false )
			*/
			$search_expr = (
				isset( $param['match'][0] ) &&
				strlen( trim($param['match'][0]) ) > 0
			) ? $param['match'][0] : false;
			if ( $search_expr ) {
				$sqlBM = (
					isset( $param['match'][1] ) &&
					$param['match'][1] == true
				) ? 'IN BOOLEAN MODE' : '';
				$o = $this->object_by_type( $o_type );
				$fts = implode( ',', $o->fulltext() );
				$this->wheres[] = "MATCH ({$fts}) AGAINST ('{$search_expr}' $sqlBM )";
				$this->wheres[] = 'AND';
				unset( $o );
				unset( $fts );
			}
			unset( $search_expr );
		}

		/** patched by k1 **/
		foreach( $param as $name => $v ) {
			if ( in_array( $name, $names ) && is_array( $v ) ) {
				$this->parse_query_param( $name, $v );
			}
		}
		/*ksort($names);
		foreach ( $names as $i => $name ) {
			if ( array_key_exists($name,$param) && is_array($param[$name]) ) {
				$this->parse_query_param( $name, $param[$name] );
			}
		}*/
		/** /patched by k1 **/

		if ( array_key_exists( 'limit', $param ) && is_int( $param['limit'] ) ) {
			$limit = ' LIMIT ' . $param['limit'];
		}

		if ( array_key_exists( 'offset', $param ) && is_int( $param['offset'] ) ) {
			$offset = ' OFFSET ' . $param['offset'];
		}

		$sql = 'SELECT ' . implode( ', ', $fields ) . " FROM {$table}";

		if ( count( $this->wheres ) ) {
			$sql .= ' WHERE '. implode( ' ', $this->wheres );
		}

		$sql .= $groups;
		$sql .= $orders;
		$sql .= $limit;
		$sql .= $offset;
		$sth = $this->dbh->prepare( $sql );

		for( $i = 0; $i < count( $this->binds ); $i++ ) {
			$sth->bindval( $i + 1, $this->binds[$i] );
		}
		return $sth;
	}

	private function parse_query_param( $name, $value ) {
		/*
			expectation:
			'name' => arrray(
				_and_ | equal, 'somename'
			)
		*/
		$flag = array_shift($value);

		if ( $flag & sql::_and_ ) {
			$this->wheres[] = 'AND';
			$flag ^= sql::_and_;
		} else if ( $flag & sql::_or_ ) {
			$this->wheres[] = 'OR';
			$flag ^= sql::_or_;
		}

		switch ($flag) {
			case sql::equal:
				$this->wheres[] = sprintf('( %s = :%d:)', $name, count($this->binds)+1 );
				$this->binds[] = $value[0];
				break;
			case (sql::equal | sql::field):
				$this->wheres[] = sprintf('( %s = %s )', $name, $value[0] );
				break;
			case sql::not_equal:
				$this->wheres[] = sprintf('( %s <> :%d:)', $name, count($this->binds)+1 );
				$this->binds[] = $value[0];
				break;
			case (sql::not_equal | sql::field):
				$this->wheres[] = sprintf('( %s <> %s )', $name, $value[0] );
				break;
			case sql::greater:
				$this->wheres[] = sprintf('( %s > :%d:)', $name, count($this->binds)+1 );
				$this->binds[] = $value[0];
				break;
			case (sql::greater | sql::field):
				$this->wheres[] = sprintf('( %s > %s )', $name, $value[0] );
				break;
			case sql::less:
				$this->wheres[] = sprintf('( %s < :%d:)', $name, count($this->binds)+1 );
				$this->binds[] = $value[0];
				break;
			case (sql::less | sql::field):
				$this->wheres[] = sprintf('( %s < %s )', $name, $value[0] );
				break;
			case (sql::equal | sql::greater):
				$this->wheres[] = sprintf('( %s >= :%d:)', $name, count($this->binds)+1 );
				$this->binds[] = $value[0];
				break;
			case (sql::equal | sql::less):
				$this->wheres[] = sprintf('( %s <= :%d:)', $name, count($this->binds)+1 );
				$this->binds[] = $value[0];
				break;
			case (sql::equal | sql::greater | sql::field):
				$this->wheres[] = sprintf('( %s >= %s )', $name, $value[0] );
				break;
			case (sql::equal | sql::less | sql::field):
				$this->wheres[] = sprintf('( %s <= %s )', $name, $value[0] );
				break;
			case sql::interval:
				$this->wheres[] = sprintf('( %s > :%d:) AND (%s < :%d:)', $name, count($this->binds)+1, $name, count($this->binds)+2 );
				$this->binds[] = $value[0];
				$this->binds[] = $value[1];
				break;
			case sql::segment:
				$this->wheres[] = sprintf('( %s >= :%d:) AND (%s <= :%d:)', $name, count($this->binds)+1, $name, count($this->binds)+2 );
				$this->binds[] = $value[0];
				$this->binds[] = $value[1];
				break;
			case sql::inset:
				$this->wheres[] = sprintf('( %s IN (:%d:) )', $name, count($this->binds)+1 );
				$this->binds[] = $value[0];
				break;
			case (sql::not_equal | sql::inset):
				$this->wheres[] = sprintf('( %s NOT IN (:%d:) )', $name, count($this->binds)+1 );
				$this->binds[] = $value[0];
				break;
			case sql::like:
				$this->wheres[] = sprintf('( %s LIKE :%d: )', $name, count($this->binds)+1 );
				$this->binds[] = $value[0];
				break;
			case sql::ilike:
				$this->wheres[] = sprintf('( %s ILIKE :%d: )', $name, count($this->binds)+1 );
				$this->binds[] = $value[0];
				break;
		}
	}

	public function get_object_list( $o_type, $param ) {
		$obj = null;
		$list = array();
		$table = $this->table_by_type( $o_type );
		$fields = $this->fields_by_type( $o_type );
		$sth = $this->parse_query_map( $o_type, $table, $fields, $fields, $param );
		$sth->execute();
		while( $row =  $sth->fetchrow_hash() ) {
			$obj = $this->object_by_type($o_type, array( 'light' => isset($param['light']) ) );
			$obj->restore($row);
			$list[] = $obj;
		}
		return $list;
	}

	public function count_object( $o_type, $param ) {
		$acount = 0;
		$table = $this->table_by_type( $o_type );
		$fields = array("count(*)");
		$wlist = $this->fields_by_type( $o_type );
		$sth = $this->parse_query_map( $o_type, $table, $fields, $wlist, $param );
		$sth->execute();
		list($acount) = $sth->fetchrow_array();
		$sth->finish();
		return $acount;
	}

}

?>
