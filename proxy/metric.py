from threading import Lock
import logging

class Gauge:
    __name: str
    __doc: str
    __type: str
    __value: float
    __default: float
    __lock: Lock
    __logger: logging.Logger

    def __init__(self, name: str, documentation: str, filename: str, default: float = 0) -> None:
        self.__name = name
        self.__doc = documentation
        self.__type = 'gauge'
        self.__value = default
        self.__default = default
        self.__lock = Lock()

        if filename:
            self.__logger = logging.getLogger(name)
            self.__logger.setLevel(logging.DEBUG)
            
            file_handler = logging.FileHandler(filename)
            formatter = logging.Formatter('%(asctime)s;%(name)s;%(message)s')
            file_handler.setFormatter(formatter)
            
            self.__logger.addHandler(file_handler)
            self.__logger.info(self.__value)
        else:
            self.__logger = None

    def get(self) -> float:
        with self.__lock:
            return self.__value
    
    def name(self) -> str:
        return self.__name
    
    def doc(self) -> str:
        return self.__doc
    
    def type(self) -> str:
        return self.__type

    def set(self, value: float) -> None:
        with self.__lock:
            self.__value = value
            if self.__logger:
                self.__logger.info(self.__value)

    def reset(self) -> None:
        self.set(self.__default)
    
    def inc(self, value: float = 1) -> None:
        with self.__lock:
            self.__value += value
            if self.__logger:
                self.__logger.info(self.__value)

    def dec(self, value: float = 1) -> None:
        self.inc(-value)
