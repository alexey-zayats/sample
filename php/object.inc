<?php

include_once 'attr.inc';

class Type {

	// abstract layer
	const Object  = 0;
	const Tree    = 1;

	// user & group
	const User    = 2;
	const Group   = 3;

	// textual structure
	const Topic   = 4;
	const Article = 5;

	const File    = 6;
	const Tie     = 7;
	const Param   = 8;
	const Flats		= 9;
	const FlatsLease = 10;
	const Houses	= 11;
	const HousesLease = 12;
	const NewHousesCity = 13;
	const NewHousesHutment = 14;
	const Genplan = 15;
	const Driftage = 16;
	const Privat = 17;
	const Jurist = 18;
	const EIRC = 19;
	const Companies = 20;
	const Exhibitions = 21;

};

class Object {
	protected $data = array();
	protected $schema = array();
	protected $table = null;
	protected $type = null;
	protected $param = array();
	protected $files = array();
	protected $_files_fetched = false;
	protected $is_light = false;
	protected $extra = array();
	protected $fulltext = array();

	public function __construct( $param = null ) {
		self::init();
		if ( isset( $param['light'] ) ) {
			$this->is_light = $param['light'];
			unset( $param['light'] );
		}
		if ( $param ) {
			$fields = array_keys( $this->schema );
			$sth = Keeper::getInstance()->parse_query_map( $this->type, $this->table, $fields, $fields, $param );
			$sth->execute();
			if ( $sth->rows() ) {
				$row = $sth->fetchrow_hash();
				$this->restore($row);
			}
		}
	}

	//---

	public function __get($name) {
		if ( isset($this->schema[$name]) && isset($this->data[$name]) ) {
			return $this->data[$name];
		}
	}

	public function __set($name,$value) {
// 		if ( isset($this->schema[$name]) && !$this->schema[$name]->ro )
		if ( isset($this->schema[$name]) )
			$this->data[$name] = $value;
	}

	//---

	public function set($name,$value) {
		if ( isset($this->schema[$name]) )
			$this->data[$name] = $value;
	}

	public function get($name) {
		if ( isset($this->schema[$name]) && isset($this->data[$name]) ) {
			return $this->data[$name];
		}
	}

	public function param($name, $value = null) {
		if ( isset($value) ) {
			if ( isset($this->param[$name]) ) {
				$this->param[$name] = array($value, 1); // update
			} else {
				$this->param[$name] = array($value, -1); // insert
			}
		}
		return isset($this->param[$name]) ? $this->param[$name] : null;
	}

	protected function init() {
		$this->type = Type::Object;
		$this->table = null;

		$this->schema['id']    = new Attribute( 'id',    Util::tr('Id'),                ViewType::Int,      true /*RO*/, false, true, false );
		$this->schema['name']  = new Attribute( 'name',  Util::tr('Name'),              ViewType::String,   false/*RO*/, false, true, true );
		$this->schema['ctime'] = new Attribute( 'ctime', Util::tr('Create time'),       ViewType::DateTime, true /*RO*/, false, false, false );
		$this->schema['mtime'] = new Attribute( 'mtime', Util::tr('Modification time'), ViewType::DateTime, true /*RO*/, false, false, false );

		$this->init();
	}

	public function remove() {
		if (!$this->id) return false;

		$sql = "DELETE FROM {$this->table} WHERE id = :1:";
		$sth = Keeper::getInstance()->dbh()->prepare($sql);
		$sth->execute($this->id);

		$sth = Keeper::getInstance()->dbh()->doit("DELETE FROM params WHERE pid = :1: AND ptable = :2:", $this->id, $this->table );
		$sth = Keeper::getInstance()->dbh()->doit("DELETE FROM files WHERE pid = :1: AND ptable = :2:", $this->id, $this->table );
		$sth = Keeper::getInstance()->dbh()->doit("DELETE FROM ties WHERE source = :1: AND source_table = :2:", $this->id, $this->table );
		$sth = Keeper::getInstance()->dbh()->doit("DELETE FROM ties WHERE dest = :1: AND dest_table = :2:", $this->id, $this->table );

		return true;
	}

	public function restore($data) {
		foreach ( $data as $key => $val ) {
			$this->data[$key] = $val;
		};

		if ( !$this->is_light ) $this->restore_param();
		if ( !$this->is_light ) $this->_fetch_files_();
	}

	public function get_file_list() {
		$this->_fetch_files_();
		return array_values($this->files);
 	}

 	private function _fetch_files_ () {
 		if ( $this->_files_fetched ) return;
 		
 		$param = array(
			'ptable' => array( sql::equal, $this->table ),
			'pid' => array( sql::_and_ | sql::equal, $this->id ),
			'light' => '1'
		);
		
		$flist = Keeper::getInstance()->get_object_list( Type::File, $param );
		foreach( $flist as $i => $f ) {
			$this->files[ $f->name ] = $f;
		}

		$this->_files_fetched = true;
 	}

 	public function get_file_object($name) {
 		$this->_fetch_files_();
 		return isset( $this->files[ $name ] ) ? $this->files[ $name ] : null;
	}

	public function restore_param() {
		$sth = Keeper::getInstance()->dbh()->prepare("SELECT name, val FROM params WHERE pid = :1: AND ptable = :2:");
		$sth->execute( $this->id, $this->table() );
		while( $row = $sth->fetchrow_array() ) {
			$this->param[ $row[0] ] = array( $row[1], 0 );
		}
	}

	public function store() {

		$fields = array();
		$holders = array();
		$binds = array();

		if ( $this->id )
			$fields[] = 'mtime = NOW()';

		$i = 1;
		while( list($key,$attr) = each( $this->schema ) ) {
			if ( $attr->ro ) continue;

			// added by k1 - generate random name if empty.
			if ( ($attr->name == 'name') && (empty($this->data[ $attr->name ])) ) {
				$this->data[ $attr->name ] = "{$this->table}_" . Util::getmicrotime();
			}

			if ( $this->id ) {
				// for update
				$fields[] = sprintf( '%s = :%d:', $attr->name, $i );
				$binds[] = isset( $this->data[ $attr->name ] ) ? $this->data[ $attr->name ] : '';
			} else {
				// for insert
				$fields[] = $attr->name;
				$holders[] = sprintf(':%d:', $i );
				$binds[] = isset($this->data[ $attr->name ]) ? $this->data[ $attr->name ] : '';
			}
			$i++;
		}

		if ( $this->id ) {
			$sql = sprintf('UPDATE %s SET %s WHERE id = :%d:', $this->table, implode(', ', $fields), $i );
		} else {
			$sql = sprintf('INSERT INTO %s ( ctime, %s ) VALUES ( NOW(), %s)', $this->table, implode(', ', $fields), implode(', ', $holders) );
		}

		$sth = Keeper::getInstance()->dbh()->prepare($sql);

		$i = 0;
		foreach ( $binds as $i => $v ) {
			$sth->bindval( $i+1, $v );
		}

		if ( $this->id ) $sth->bindval( $i+2, $this->id );
		$sth->execute();

		if ( !$this->id ) $this->id = Keeper::getInstance()->dbh()->last_id();

		foreach ( $this->param as $key => $a ) {
			if ( $a[1] == 1 ) {
				Keeper::getInstance()->dbh()->doit(
					"UPDATE params SET val = :1: WHERE name = :2: AND pid = :3: AND ptable = :4:",
					$a[0], $key, $this->id, $this->table() );
			} else if ( $a[1] == -1 ) {
				Keeper::getInstance()->dbh()->doit("INSERT INTO params (name, val, pid, ptable) VALUES ( :1:, :2:, :3:, :4: )",
					$key, $a[0], $this->id, $this->table() );
			}
		}
	}

	public function table() { return $this->table; }
	public function fulltext() { return $this->fulltext; }
	public function schema() { return array_keys( $this->schema ); }
	public function attribute($name) { return isset($this->schema[$name]) ? $this->schema[$name] : null; }
	public function data() { return $this->data; }

	public function as_xml () {
		$out = array();
		foreach ( $this->data as $key => $val ) {
			$out[] = sprintf( '<%s><![CDATA[%s]]></%s>', $key, $val, $key );
		}
		foreach ( $this->param as $key => $val ) {
			$out[] = sprintf( '<%s><![CDATA[%s]]></%s>', $key, $val[0], $key );
		}
		return implode("\n", $out);
	}

	public function get_extra( $k = null ) {
		if ( $k ) {
			return ( isset($this->extra[ $k ]) ) ? $this->extra[ $k ] : null;
		} else return $this->extra;
	}

	public function set_extra( $k, $v ) {
		$this->extra[ $k ] = $v;
	}

}

?>
