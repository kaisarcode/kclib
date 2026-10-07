<?php
/**
 * Summary: REDP2P index example
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GPL-3.0
 */
declare(strict_types=1);

require __DIR__ . '../imp/php/Redp2pIndex.php';

$server = new Redp2pIndex([
    'dsn' => 'sqlite:' . __DIR__ . '/redp2p.sqlite',
]);
$server->serve();
