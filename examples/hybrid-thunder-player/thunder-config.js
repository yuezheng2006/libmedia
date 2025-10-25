/**
 * Thunder Player SDK 全局配置
 * ToB客户配置文件 - 每个客户需要根据自己的环境修改此文件
 */

// 认证服务器配置 - 必须配置为客户的认证服务器地址
window.THUNDER_AUTH_SERVER = 'https://ls6d8kng.ktvsky.com';

// 可选配置项（未来扩展）
window.THUNDER_CONFIG = {
    // 认证服务器
    authServer: 'https://ls6d8kng.ktvsky.com',
    
    // 其他可能的配置项
    // apiTimeout: 30000,
    // retryCount: 3,
    // logLevel: 'info'
};

console.log('Thunder Player SDK 配置已加载:', window.THUNDER_AUTH_SERVER);
